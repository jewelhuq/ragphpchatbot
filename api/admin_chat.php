<?php

// ============================================================
// admin_chat.php — Admin API for live chat takeover
// ============================================================
// Auth required. This is the control room side of the live-chat
// feature. An admin opens the Conversations panel, spots a
// visitor who needs a human touch, and hits "Take Over".
//
// From that moment:
//   1. chat_sessions.mode flips to 'human'
//   2. The widget's next poll picks up the mode change and stops
//      sending queries to the AI endpoint
//   3. Every message the admin types here is inserted with
//      role='admin' and lands in the widget's next poll response
//   4. When the admin is done, they hit "Release" and the AI
//      resumes answering as if nothing happened
//
// Actions:
//   POST ?action=takeover&session_id=X   — admin claims this session
//   POST ?action=release&session_id=X    — admin hands it back to AI
//   POST ?action=send&session_id=X       — admin sends a message
//   GET  ?action=active                  — list sessions in 'human' mode
//   GET  ?action=messages&session_id=X&since=0 — poll for admin panel
// ============================================================

require_once __DIR__ . '/bootstrap.php';

// Every request must carry a valid Bearer token.
$account = requireAuth($auth);

// ----------------------------------------------------------
// Ensure the live-chat columns exist on chat_sessions.
// SQLite has no "ADD COLUMN IF NOT EXISTS", so we attempt
// each ALTER and silently swallow the "duplicate column" error.
// Both this file and live_chat.php do this — whichever runs
// first wins, and the other is harmless.
// ----------------------------------------------------------
try {
    $turso->execute("ALTER TABLE chat_sessions ADD COLUMN mode TEXT NOT NULL DEFAULT 'ai'");
} catch (\Exception $e) {
    // Column already exists — carry on.
}

try {
    $turso->execute("ALTER TABLE chat_sessions ADD COLUMN admin_id INTEGER");
} catch (\Exception $e) {
    // Column already exists — carry on.
}

$action    = $_GET['action']     ?? '';
$sessionId = $_GET['session_id'] ?? '';

match ($action) {

    // ----------------------------------------------------------------
    // POST ?action=takeover&session_id=X
    //
    // The admin clicks "Take Over" in the panel. We flip the session
    // mode to 'human' and record which admin account claimed it.
    // The widget will notice within 2 seconds on its next poll.
    // ----------------------------------------------------------------
    'takeover' => (function () use ($turso, $account, $sessionId) {
        if ($sessionId === '') json_error('Missing session_id');

        $session = $turso->fetchAll(
            "SELECT id FROM chat_sessions WHERE session_id = ?",
            [$sessionId]
        );
        if (empty($session)) json_error('Session not found', 404);

        $turso->execute(
            "UPDATE chat_sessions
             SET mode = 'human', admin_id = ?
             WHERE session_id = ?",
            [(int)$account['id'], $sessionId]
        );

        json_ok(null);
    })(),

    // ----------------------------------------------------------------
    // POST ?action=release&session_id=X
    //
    // The admin is done. Mode reverts to 'ai', admin_id is cleared.
    // The widget's next poll returns mode='ai' and the AI starts
    // answering again on the visitor's next question.
    // ----------------------------------------------------------------
    'release' => (function () use ($turso, $sessionId) {
        if ($sessionId === '') json_error('Missing session_id');

        $session = $turso->fetchAll(
            "SELECT id FROM chat_sessions WHERE session_id = ?",
            [$sessionId]
        );
        if (empty($session)) json_error('Session not found', 404);

        $turso->execute(
            "UPDATE chat_sessions
             SET mode = 'ai', admin_id = NULL
             WHERE session_id = ?",
            [$sessionId]
        );

        json_ok(null);
    })(),

    // ----------------------------------------------------------------
    // POST ?action=send&session_id=X
    // body: { "content": "Hello, I can help you with that!" }
    //
    // The admin types a reply and hits Send. The message is inserted
    // with role='admin' so the widget can style it differently from
    // AI responses. last_active is bumped so the session stays at
    // the top of the conversations list.
    // ----------------------------------------------------------------
    'send' => (function () use ($turso, $account, $sessionId) {
        if ($sessionId === '') json_error('Missing session_id');

        $body    = requestBody();
        $content = trim($body['content'] ?? '');
        if ($content === '') json_error('Message content cannot be empty');

        // Confirm the session exists before we write anything.
        $session = $turso->fetchAll(
            "SELECT id, mode FROM chat_sessions WHERE session_id = ?",
            [$sessionId]
        );
        if (empty($session)) json_error('Session not found', 404);

        // Warn if the admin is trying to send while mode is still 'ai'.
        // We allow it anyway — the admin may be sending a follow-up
        // just before the release — but a soft guard is helpful.
        // (Uncomment to make this a hard block instead.)
        // if ($session[0]['mode'] !== 'human') json_error('Session is not in human mode');

        // Insert the admin message.
        $turso->execute(
            "INSERT INTO chat_messages (session_id, role, content) VALUES (?, 'admin', ?)",
            [$sessionId, $content]
        );

        // Fetch the row we just inserted so we can return it fully formed.
        $inserted = $turso->fetchAll(
            "SELECT id, role, content, created_at
             FROM chat_messages
             WHERE session_id = ? AND role = 'admin'
             ORDER BY id DESC LIMIT 1",
            [$sessionId]
        );

        // Keep last_active fresh — the session stays pinned at the top.
        $turso->execute(
            "UPDATE chat_sessions
             SET last_active = datetime('now'),
                 message_count = message_count + 1
             WHERE session_id = ?",
            [$sessionId]
        );

        json_ok(['message' => $inserted[0]]);
    })(),

    // ----------------------------------------------------------------
    // GET ?action=active
    //
    // Returns every session currently in 'human' mode that belongs to
    // a workspace this admin owns. Useful for the "Live Chats" badge
    // or a dedicated tray in the admin panel showing active handoffs.
    // ----------------------------------------------------------------
    'active' => (function () use ($turso, $account) {
        $sessions = $turso->fetchAll(
            "SELECT cs.id, cs.session_id, cs.workspace_id,
                    cs.visitor_name, cs.visitor_phone, cs.visitor_email,
                    cs.started_at, cs.last_active, cs.message_count,
                    cs.mode, cs.admin_id
             FROM chat_sessions cs
             JOIN workspaces w ON w.id = cs.workspace_id
             WHERE cs.mode = 'human'
               AND w.account_id = ?
             ORDER BY cs.last_active DESC",
            [(int)$account['id']]
        );

        json_ok($sessions);
    })(),

    // ----------------------------------------------------------------
    // GET ?action=messages&session_id=X&since=0
    //
    // The admin panel polls this for its own live view of the thread.
    // Works identically to the widget's poll action in live_chat.php
    // but lives behind auth — the admin sees everything including the
    // 'user', 'assistant', and 'admin' roles so they can follow the
    // full timeline of the conversation.
    // ----------------------------------------------------------------
    'messages' => (function () use ($turso, $sessionId) {
        if ($sessionId === '') json_error('Missing session_id');

        $since = (int)($_GET['since'] ?? 0);

        $session = $turso->fetchAll(
            "SELECT mode, admin_id FROM chat_sessions WHERE session_id = ?",
            [$sessionId]
        );
        if (empty($session)) json_error('Session not found', 404);

        $messages = $turso->fetchAll(
            "SELECT id, role, content, created_at
             FROM chat_messages
             WHERE session_id = ? AND id > ?
             ORDER BY id ASC",
            [$sessionId, $since]
        );

        json_ok([
            'mode'     => $session[0]['mode'],
            'admin_id' => $session[0]['admin_id'],
            'messages' => $messages,
        ]);
    })(),

    default => json_error("Unknown action: $action")
};
