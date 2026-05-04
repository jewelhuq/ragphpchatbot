<?php

// ============================================================
// conversations.php — Admin API for viewing chat sessions
// ============================================================
// Auth required. Returns sessions list and full message threads
// for the admin panel Conversations page.
// ============================================================

require_once __DIR__ . '/bootstrap.php';

$account = requireAuth($auth);
$action  = $_GET['action'] ?? 'list';
$wsId    = (int)($_GET['workspace_id'] ?? 0);

match ($action) {

    // ----------------------------------------------------------------
    // GET ?action=list&workspace_id=X
    // Returns all chat sessions for a workspace, newest first.
    // Shows visitor name, contact info, message count, last active.
    // ----------------------------------------------------------------
    'list' => (function () use ($turso, $wsId) {
        $sessions = $turso->fetchAll(
            "SELECT id, session_id, visitor_name, visitor_phone, visitor_email,
                    started_at, last_active, message_count
             FROM chat_sessions
             WHERE workspace_id = ?
             ORDER BY last_active DESC",
            [$wsId]
        );
        json_ok($sessions);
    })(),

    // ----------------------------------------------------------------
    // GET ?action=view&session_id=xxx
    // Returns full conversation thread for a specific session.
    // ----------------------------------------------------------------
    'view' => (function () use ($turso) {
        $sessionId = $_GET['session_id'] ?? '';
        if ($sessionId === '') json_error('Missing session_id');

        $session = $turso->fetchAll(
            "SELECT * FROM chat_sessions WHERE session_id = ?", [$sessionId]
        );
        if (empty($session)) json_error('Session not found', 404);

        $messages = $turso->fetchAll(
            "SELECT role, content, created_at
             FROM chat_messages
             WHERE session_id = ?
             ORDER BY id ASC",
            [$sessionId]
        );

        json_ok(['session' => $session[0], 'messages' => $messages]);
    })(),

    // ----------------------------------------------------------------
    // DELETE ?action=delete&session_id=xxx
    // Deletes a session and all its messages.
    // ----------------------------------------------------------------
    'delete' => (function () use ($turso) {
        $sessionId = $_GET['session_id'] ?? '';
        if ($sessionId === '') json_error('Missing session_id');

        $turso->execute("DELETE FROM chat_messages WHERE session_id = ?", [$sessionId]);
        $turso->execute("DELETE FROM chat_sessions WHERE session_id = ?", [$sessionId]);
        json_ok(['deleted' => true]);
    })(),

    default => json_error("Unknown action: $action")
};
