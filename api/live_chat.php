<?php

// ============================================================
// live_chat.php — Public polling endpoint for the widget
// ============================================================
// Called by the embedded widget every 2 seconds so the visitor
// can receive admin messages in real-time. No auth required —
// the session_id IS the shared secret the visitor already holds.
//
// The story: a visitor is mid-conversation with the AI when
// an admin decides to step in. From this moment forward, every
// message the admin types appears here within 2 seconds. This
// file is the mailbox the widget keeps checking.
//
// Actions:
//   GET ?action=poll&session_id=X&last_message_id=0
//       Returns any messages newer than last_message_id.
//       Also returns the current session mode so the widget
//       knows whether to show the AI input or a "connected
//       with support" banner.
//
//   GET ?action=status&session_id=X
//       Lightweight probe — just returns the mode ('ai' or 'human').
//       Used on page-load so the widget can hydrate correctly.
// ============================================================

require_once __DIR__ . '/../vendor/autoload.php';

use RAG\Turso;

$config = require __DIR__ . '/../config.php';

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }

$turso = new Turso($config);

// ----------------------------------------------------------
// Tiny local helpers — mirror sessions.php style intentionally
// so any dev reading both files feels at home immediately.
// ----------------------------------------------------------
function ok(mixed $data): never  { echo json_encode(['ok' => true,  'data'  => $data]); exit; }
function err(string $msg): never { echo json_encode(['ok' => false, 'error' => $msg]);  exit; }

// ----------------------------------------------------------
// Ensure the live-chat columns exist on chat_sessions.
// SQLite has no "ADD COLUMN IF NOT EXISTS", so we attempt
// the ALTER and silently swallow the "duplicate column" error.
// The base CREATE TABLE lives in sessions.php; we only bolt on
// the two new columns we need.
// ----------------------------------------------------------
try {
    $turso->execute("ALTER TABLE chat_sessions ADD COLUMN mode TEXT NOT NULL DEFAULT 'ai'");
} catch (\Exception $e) {
    // Column already exists — perfectly fine, carry on.
}

try {
    $turso->execute("ALTER TABLE chat_sessions ADD COLUMN admin_id INTEGER");
} catch (\Exception $e) {
    // Column already exists — perfectly fine, carry on.
}

$action    = $_GET['action']    ?? '';
$sessionId = $_GET['session_id'] ?? '';

if ($sessionId === '') err('Missing session_id');

match ($action) {

    // ----------------------------------------------------------------
    // GET ?action=poll&session_id=X&last_message_id=0
    //
    // The widget calls this every 2 seconds. We return:
    //   - mode: 'ai' or 'human' — tells the widget whether an admin
    //     is live on the other end
    //   - messages: only rows newer than last_message_id, so the
    //     widget can append incrementally without re-rendering the
    //     whole thread
    //
    // The widget increments last_message_id to the highest id it
    // received, so each poll is a lightweight diff, not a full reload.
    // ----------------------------------------------------------------
    'poll' => (function () use ($turso, $sessionId) {
        $lastId = (int)($_GET['last_message_id'] ?? 0);

        // Grab the session mode first — one row, one column.
        $session = $turso->fetchAll(
            "SELECT mode FROM chat_sessions WHERE session_id = ?",
            [$sessionId]
        );
        if (empty($session)) err('Session not found');

        $mode = $session[0]['mode'] ?? 'ai';

        // Fetch only messages the widget hasn't seen yet.
        $messages = $turso->fetchAll(
            "SELECT id, role, content, created_at
             FROM chat_messages
             WHERE session_id = ? AND id > ?
             ORDER BY id ASC",
            [$sessionId, $lastId]
        );

        ok(['mode' => $mode, 'messages' => $messages]);
    })(),

    // ----------------------------------------------------------------
    // GET ?action=status&session_id=X
    //
    // A lightweight check used on page-load (and occasionally when
    // the widget resumes after a network hiccup). Returns only the
    // mode so the widget can decide how to render the input area
    // without pulling down the entire message list.
    // ----------------------------------------------------------------
    'status' => (function () use ($turso, $sessionId) {
        $session = $turso->fetchAll(
            "SELECT mode FROM chat_sessions WHERE session_id = ?",
            [$sessionId]
        );
        if (empty($session)) err('Session not found');

        ok(['mode' => $session[0]['mode'] ?? 'ai']);
    })(),

    default => err("Unknown action: $action")
};
