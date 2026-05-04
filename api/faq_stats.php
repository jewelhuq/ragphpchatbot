<?php
/**
 * faq_stats.php — Admin analytics endpoint for FAQ performance data.
 *
 * This file exists because the admin panel needs visibility into how users
 * are actually interacting with FAQ content — which questions get seen,
 * which ones users find helpful, and which ones land with a thumbs down.
 * Without this data, improving the FAQ is guesswork.
 *
 * Auth is required: this is internal data (IPs, sessions, vote patterns)
 * that should never be exposed to the public widget or unauthenticated callers.
 */

require_once __DIR__ . '/bootstrap.php';

// Gate everything behind admin authentication first.
// If the token is missing or invalid, requireAuth() will halt execution
// and emit a 401 before we touch any database logic.
requireAuth($auth);

$action       = $_GET['action']       ?? '';
$workspace_id = $_GET['workspace_id'] ?? '';

// A workspace_id is mandatory for every action — without it we'd be
// querying across all tenants, which is both a security concern and
// a performance disaster on a shared database.
if ($workspace_id === '') {
    json_error('workspace_id is required', 400);
}

// ---------------------------------------------------------------------------
// ACTION: overview
// Returns every FAQ item enriched with view counts, unique visitor counts,
// and aggregated vote tallies — the full picture for a single workspace.
// ---------------------------------------------------------------------------
if ($action === 'overview') {
    // We LEFT JOIN faq_views and faq_votes so that FAQ items with zero
    // views or zero votes still appear in the result rather than being
    // silently dropped by an INNER JOIN. An FAQ that nobody has seen is
    // still worth knowing about — it might need better discoverability.
    //
    // COUNT(DISTINCT fv.id) gives total view events (repeat visits count).
    // COUNT(DISTINCT fv.visitor_ip) gives unique visitors, which matters
    // when deciding whether low engagement is a reach problem or a content
    // problem. A question seen 100 times by 1 IP is very different from
    // one seen 100 times by 100 different people.
    //
    // The conditional SUM pattern (SUM(CASE WHEN ...)) lets us pivot the
    // normalised vote rows into two separate columns in one pass, avoiding
    // a subquery or a second round-trip to the database.
    //
    // ORDER BY view_count DESC surfaces the most-trafficked FAQs first,
    // which is almost always what the admin wants to review first.
    $sql = "
        SELECT
            f.id,
            f.question,
            f.answer,
            COUNT(DISTINCT fv.id)         AS view_count,
            COUNT(DISTINCT fv.visitor_ip) AS unique_viewers,
            SUM(CASE WHEN vt.vote = 'up'   THEN 1 ELSE 0 END) AS up_votes,
            SUM(CASE WHEN vt.vote = 'down' THEN 1 ELSE 0 END) AS down_votes
        FROM faq f
        LEFT JOIN faq_views fv ON fv.faq_id = f.id
        LEFT JOIN faq_votes vt ON vt.faq_id = f.id
        WHERE f.workspace_id = ?
        GROUP BY f.id
        ORDER BY view_count DESC
    ";

    $result = $turso->query($sql, [$workspace_id]);

    // Enrich each row with derived metrics that the frontend would otherwise
    // have to compute itself — keeping that logic in one place avoids drift
    // if the formula ever changes.
    $rows = [];
    foreach ($result['rows'] as $row) {
        $up   = (int) $row['up_votes'];
        $down = (int) $row['down_votes'];
        $total_votes = $up + $down;

        // score is the raw net sentiment: positive means more approvals
        // than rejections, negative means the opposite.
        $score = $up - $down;

        // vote_ratio is the percentage of votes that were positive.
        // We return null instead of 0 when there are no votes at all,
        // so the frontend can distinguish "perfectly balanced" (50.0) from
        // "no data yet" (null) without needing a separate flag field.
        $vote_ratio = $total_votes > 0
            ? round(($up / $total_votes) * 100, 1)
            : null;

        $rows[] = [
            'id'             => $row['id'],
            'question'       => $row['question'],
            'answer'         => $row['answer'],
            'view_count'     => (int) $row['view_count'],
            'unique_viewers' => (int) $row['unique_viewers'],
            'up_votes'       => $up,
            'down_votes'     => $down,
            'score'          => $score,
            'vote_ratio'     => $vote_ratio,
        ];
    }

    json_ok($rows);
}

// ---------------------------------------------------------------------------
// ACTION: recent
// Returns the most recent view events so admins can watch real-time traffic
// or investigate a spike. Capped by ?limit= so the payload stays manageable.
// ---------------------------------------------------------------------------
elseif ($action === 'recent') {
    // Default to 50 if the caller doesn't specify — enough to show a useful
    // feed without overwhelming the browser or the network response budget.
    // We cast to int and clamp to a sane ceiling (500) to prevent a
    // malicious or misconfigured client from dumping the whole table.
    $limit = min((int) ($_GET['limit'] ?? 50), 500);
    if ($limit < 1) {
        $limit = 50;
    }

    // We JOIN faq here (not LEFT JOIN) because a view record without a
    // matching FAQ row indicates corrupted/orphaned data that we don't
    // want surfacing in the analytics feed — it would confuse the admin
    // with a blank question column and no meaningful action to take.
    //
    // visitor_ip and session_id are included because admins sometimes need
    // to investigate abuse (e.g., a bot inflating view counts) or trace a
    // support session back to a specific user journey.
    $sql = "
        SELECT
            fv.id,
            fv.faq_id,
            f.question,
            fv.visitor_ip,
            fv.session_id,
            fv.viewed_at
        FROM faq_views fv
        JOIN faq f ON f.id = fv.faq_id
        WHERE fv.workspace_id = ?
        ORDER BY fv.viewed_at DESC
        LIMIT ?
    ";

    $result = $turso->query($sql, [$workspace_id, $limit]);

    json_ok($result['rows']);
}

// ---------------------------------------------------------------------------
// ACTION: votes
// Returns the full vote log with voter context. Useful for spotting patterns
// like a single IP repeatedly downvoting one FAQ, or seeing which answers
// consistently earn approval from diverse visitors.
// ---------------------------------------------------------------------------
elseif ($action === 'votes') {
    // Same JOIN reasoning as the recent-views query: an orphaned vote row
    // without a corresponding FAQ entry is noise, not signal.
    //
    // ORDER BY voted_at DESC means the newest votes appear first — admins
    // reviewing feedback typically want to see the latest reactions, not
    // dig through historical data from months ago.
    $sql = "
        SELECT
            vt.faq_id,
            f.question,
            vt.visitor_ip,
            vt.session_id,
            vt.vote,
            vt.voted_at
        FROM faq_votes vt
        JOIN faq f ON f.id = vt.faq_id
        WHERE vt.workspace_id = ?
        ORDER BY vt.voted_at DESC
    ";

    $result = $turso->query($sql, [$workspace_id]);

    json_ok($result['rows']);
}

// ---------------------------------------------------------------------------
// Fallback — unrecognised or missing action.
// We return a 400 with the list of valid actions so the caller immediately
// knows what they got wrong without having to read the source code.
// ---------------------------------------------------------------------------
else {
    json_error('Unknown action. Valid actions: overview, recent, votes', 400);
}
