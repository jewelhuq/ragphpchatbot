<?php

// ============================================================
// faq_analytics.php — Public FAQ analytics endpoint for the widget
// ============================================================
// This endpoint is intentionally PUBLIC — no auth token required.
//
// The embedded widget needs to silently report views and collect
// votes from anonymous visitors without them ever knowing a login
// session exists. Gating this behind auth would break the widget
// for every end-user who isn't an admin.
//
// Actions (dispatched via ?action=):
//   POST ?action=view      — record that a visitor viewed a FAQ item
//   POST ?action=vote      — record or update a visitor's up/down vote
//   GET  ?action=stats     — aggregate view/vote counts per FAQ item
//   GET  ?action=my_vote   — fetch this visitor's current vote on an item
// ============================================================

require_once __DIR__ . '/../vendor/autoload.php';

use RAG\Turso;

$config = require __DIR__ . '/../config.php';

// ----------------------------------------------------------
// CORS + Content-Type
//
// The widget is embedded on third-party domains. The wildcard
// origin is correct here — this is anonymous, public data.
// We also handle OPTIONS preflight so browsers don't block
// the POST requests before they even start.
// ----------------------------------------------------------
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

// ----------------------------------------------------------
// Helpers
//
// Thin wrappers so every exit path reads as plain English.
// ----------------------------------------------------------
function json_ok_a(mixed $data = null, int $code = 200): never
{
    http_response_code($code);
    echo json_encode(['ok' => true, 'data' => $data]);
    exit;
}

function json_error_a(string $message, int $code = 400): never
{
    http_response_code($code);
    echo json_encode(['ok' => false, 'error' => $message]);
    exit;
}

function request_body_a(): array
{
    $raw = file_get_contents('php://input');
    return json_decode($raw, true) ?? [];
}

// ----------------------------------------------------------
// Visitor IP resolution
//
// When the app sits behind a load balancer or reverse proxy,
// REMOTE_ADDR will be the proxy's IP, not the real visitor.
// HTTP_X_FORWARDED_FOR carries the original client IP in that
// case. We trust the first address in the XFF chain because
// that is the one the client itself reported (subsequent hops
// are added by proxies we control and can't be spoofed by
// the visitor).
//
// We deliberately don't store precise IPs — we only use them
// to enforce "one vote per visitor per FAQ item" without
// requiring a login. The IP is pseudonymous, not PII-level.
// ----------------------------------------------------------
function visitor_ip(): string
{
    $xff = $_SERVER['HTTP_X_FORWARDED_FOR'] ?? '';
    if ($xff !== '') {
        // XFF can be "client, proxy1, proxy2" — take leftmost
        $parts = explode(',', $xff);
        return trim($parts[0]);
    }
    return $_SERVER['REMOTE_ADDR'] ?? '0.0.0.0';
}

// ----------------------------------------------------------
// Database bootstrap
// ----------------------------------------------------------
$turso = new Turso($config);

// ----------------------------------------------------------
// Schema migration — ensure our two analytics tables exist.
//
// We create them here, on every request, because this is a
// public endpoint with no admin bootstrap step. IF NOT EXISTS
// makes the CREATE idempotent and nearly free after the first
// call (SQLite checks the schema cache before touching disk).
//
// faq_views: intentionally allows duplicate rows for the same
// visitor. A user who reads the same FAQ three times should
// count as three views — repeated reading signals genuine
// interest and is useful for sorting/ranking.
//
// faq_votes: UNIQUE on (faq_id, visitor_ip) enforces the
// one-vote-per-visitor rule at the database level, not just
// the application layer. Using INSERT OR REPLACE lets us flip
// an existing vote (up → down) in a single statement without
// a SELECT-then-UPDATE round trip.
// ----------------------------------------------------------
$turso->execute("
    CREATE TABLE IF NOT EXISTS faq_views (
        id           INTEGER PRIMARY KEY AUTOINCREMENT,
        faq_id       INTEGER NOT NULL,
        workspace_id INTEGER NOT NULL,
        visitor_ip   TEXT    NOT NULL,
        session_id   TEXT,
        viewed_at    TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
    )
");

$turso->execute("
    CREATE TABLE IF NOT EXISTS faq_votes (
        id           INTEGER PRIMARY KEY AUTOINCREMENT,
        faq_id       INTEGER NOT NULL,
        workspace_id INTEGER NOT NULL,
        visitor_ip   TEXT    NOT NULL,
        session_id   TEXT,
        vote         TEXT    NOT NULL CHECK(vote IN ('up','down')),
        voted_at     TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
        UNIQUE(faq_id, visitor_ip)
    )
");

// ----------------------------------------------------------
// Widget → Workspace resolver
//
// Every public action receives a widget_id (a public token
// embedded in the snippet). We translate that into an internal
// workspace_id so analytics are scoped correctly. Failing fast
// on an unknown widget prevents phantom rows from polluting
// the database.
// ----------------------------------------------------------
function resolve_workspace(Turso $turso, string $widgetId): int
{
    if ($widgetId === '') {
        json_error_a('Missing widget_id.');
    }

    $rows = $turso->fetchAll(
        "SELECT id FROM workspaces WHERE widget_id = ? LIMIT 1",
        [$widgetId]
    );

    if (empty($rows)) {
        json_error_a('Widget not found.', 404);
    }

    return (int)$rows[0]['id'];
}

// ----------------------------------------------------------
// Action dispatch
// ----------------------------------------------------------
$action = $_GET['action'] ?? '';

try {
    match ($action) {

        // ======================================================
        // POST ?action=view
        // body: { faq_id, widget_id, session_id? }
        //
        // The widget fires this silently whenever a visitor
        // opens/expands a FAQ accordion item. We record every
        // occurrence — no deduplication — because the raw count
        // is the metric the admin dashboard cares about.
        // ======================================================
        'view' => (function () use ($turso): never {
            $body      = request_body_a();
            $faqId     = (int)($body['faq_id']    ?? 0);
            $widgetId  = trim($body['widget_id']   ?? '');
            $sessionId = trim($body['session_id']  ?? '');

            if ($faqId === 0) {
                json_error_a('Missing faq_id.');
            }

            $workspaceId = resolve_workspace($turso, $widgetId);
            $ip          = visitor_ip();

            // Insert without any UNIQUE guard — deliberate.
            // Each page load / accordion click is a distinct view event.
            $turso->execute(
                "INSERT INTO faq_views (faq_id, workspace_id, visitor_ip, session_id)
                 VALUES (?, ?, ?, ?)",
                [$faqId, $workspaceId, $ip, $sessionId ?: null]
            );

            json_ok_a(null);
        })(),

        // ======================================================
        // POST ?action=vote
        // body: { faq_id, widget_id, vote ("up"|"down"), session_id? }
        //
        // We use INSERT OR REPLACE so the visitor can freely
        // change their mind (up → down or vice-versa) without
        // us needing a SELECT first. The UNIQUE constraint on
        // (faq_id, visitor_ip) is what triggers the REPLACE
        // branch when a row already exists.
        // ======================================================
        'vote' => (function () use ($turso): never {
            $body      = request_body_a();
            $faqId     = (int)($body['faq_id']    ?? 0);
            $widgetId  = trim($body['widget_id']   ?? '');
            $vote      = trim($body['vote']        ?? '');
            $sessionId = trim($body['session_id']  ?? '');

            if ($faqId === 0) {
                json_error_a('Missing faq_id.');
            }
            if (!in_array($vote, ['up', 'down'], true)) {
                json_error_a('vote must be "up" or "down".');
            }

            $workspaceId = resolve_workspace($turso, $widgetId);
            $ip          = visitor_ip();

            // INSERT OR REPLACE: if the UNIQUE constraint fires (same
            // faq_id + visitor_ip already exists), SQLite deletes the
            // old row and inserts the new one. This effectively updates
            // the vote value without a separate UPDATE statement.
            $turso->execute(
                "INSERT OR REPLACE INTO faq_votes
                    (faq_id, workspace_id, visitor_ip, session_id, vote, voted_at)
                 VALUES (?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'))",
                [$faqId, $workspaceId, $ip, $sessionId ?: null, $vote]
            );

            json_ok_a(['vote' => $vote]);
        })(),

        // ======================================================
        // GET ?action=stats&widget_id=X
        //
        // Returns every FAQ item for this workspace enriched with
        // aggregated analytics: total view events, up-vote count,
        // and down-vote count.
        //
        // The LEFT JOINs are critical — we want to see FAQ items
        // that have zero views or zero votes too, not just the
        // popular ones. COALESCE turns the NULLs from empty LEFT
        // JOINs into clean zeros.
        //
        // We group by f.id (not f.question) to handle the edge
        // case where two FAQ items happen to share identical
        // question text but have different answers.
        // ======================================================
        'stats' => (function () use ($turso): never {
            $widgetId    = trim($_GET['widget_id'] ?? '');
            $workspaceId = resolve_workspace($turso, $widgetId);

            $rows = $turso->fetchAll(
                "SELECT
                    f.id,
                    f.question,
                    COALESCE(COUNT(DISTINCT fv.id), 0)                                AS view_count,
                    COALESCE(SUM(CASE WHEN votes.vote = 'up'   THEN 1 ELSE 0 END), 0) AS up_votes,
                    COALESCE(SUM(CASE WHEN votes.vote = 'down' THEN 1 ELSE 0 END), 0) AS down_votes
                 FROM faq f
                 LEFT JOIN faq_views fv    ON fv.faq_id    = f.id
                 LEFT JOIN faq_votes votes ON votes.faq_id = f.id
                 WHERE f.workspace_id = ?
                 GROUP BY f.id
                 ORDER BY view_count DESC",
                [$workspaceId]
            );

            // Cast the aggregate columns to integers — SQLite returns them
            // as strings, and the dashboard JS will do arithmetic on them.
            $items = array_map(function (array $row): array {
                return [
                    'id'          => (int)$row['id'],
                    'question'    => $row['question'],
                    'view_count'  => (int)$row['view_count'],
                    'up_votes'    => (int)$row['up_votes'],
                    'down_votes'  => (int)$row['down_votes'],
                ];
            }, $rows);

            json_ok_a($items);
        })(),

        // ======================================================
        // GET ?action=my_vote&faq_id=X&widget_id=X
        //
        // Lets the widget restore the correct button state when
        // a returning visitor opens the FAQ panel. Without this,
        // the "thumbs up" button would appear un-pressed even
        // though the visitor already voted.
        //
        // We identify the visitor by IP alone (no session cookie
        // needed) so this works even across browser restarts.
        // ======================================================
        'my_vote' => (function () use ($turso): never {
            $faqId    = (int)trim($_GET['faq_id']    ?? '0');
            $widgetId = trim($_GET['widget_id'] ?? '');

            if ($faqId === 0) {
                json_error_a('Missing faq_id.');
            }

            $workspaceId = resolve_workspace($turso, $widgetId);
            $ip          = visitor_ip();

            $rows = $turso->fetchAll(
                "SELECT vote FROM faq_votes
                 WHERE faq_id = ? AND visitor_ip = ?
                 LIMIT 1",
                [$faqId, $ip]
            );

            // Return null if no vote exists — the widget interprets
            // null as "neither button highlighted" which is correct.
            $currentVote = !empty($rows) ? $rows[0]['vote'] : null;

            json_ok_a(['vote' => $currentVote]);
        })(),

        default => json_error_a("Unknown action: '$action'.")

    };
} catch (\Exception $e) {
    // Catch-all: surface the message in dev, but never let an
    // unhandled exception produce a non-JSON response that would
    // break the widget's JSON.parse() call.
    json_error_a($e->getMessage(), 500);
}
