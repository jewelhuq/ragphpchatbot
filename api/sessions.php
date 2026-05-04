<?php

// ============================================================
// sessions.php — Public API for Widget Chat Sessions
// ============================================================
// This endpoint is called by the widget (no auth required).
// It handles:
//   - Creating a new session when a visitor starts chatting
//   - Saving each message (user + assistant) as it happens
//   - Loading a session's history so it survives page refresh
//
// All data is scoped to a workspace via widget_id.
// ============================================================

require_once __DIR__ . '/../vendor/autoload.php';

use RAG\Turso;

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }

$config = require __DIR__ . '/../config.php';
$turso  = new Turso($config);

function ok(mixed $data): never  { echo json_encode(['ok' => true,  'data'  => $data]);  exit; }
function err(string $msg): never { echo json_encode(['ok' => false, 'error' => $msg]);   exit; }
function body(): array { return json_decode(file_get_contents('php://input'), true) ?? []; }

// ----------------------------------------------------------------
// Ensure tables exist on every request (safe — IF NOT EXISTS)
// ----------------------------------------------------------------
$turso->execute("
    CREATE TABLE IF NOT EXISTS chat_sessions (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        workspace_id INTEGER NOT NULL,
        session_id  TEXT    NOT NULL UNIQUE,
        visitor_name  TEXT  NOT NULL DEFAULT '',
        visitor_phone TEXT  NOT NULL DEFAULT '',
        visitor_email TEXT  NOT NULL DEFAULT '',
        started_at  TEXT    NOT NULL DEFAULT (datetime('now')),
        last_active TEXT    NOT NULL DEFAULT (datetime('now')),
        message_count INTEGER NOT NULL DEFAULT 0
    )
");

$turso->execute("
    CREATE INDEX IF NOT EXISTS idx_sessions_workspace
    ON chat_sessions(workspace_id)
");

$turso->execute("
    CREATE TABLE IF NOT EXISTS chat_messages (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id  TEXT    NOT NULL,
        role        TEXT    NOT NULL,
        content     TEXT    NOT NULL,
        created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
    )
");

$turso->execute("
    CREATE INDEX IF NOT EXISTS idx_messages_session
    ON chat_messages(session_id)
");

$action = $_GET['action'] ?? '';

match ($action) {

    // ----------------------------------------------------------------
    // POST ?action=start
    // Called when visitor submits their name (and optionally phone/email).
    // Creates a new session and returns a unique session_id.
    // The widget stores this in localStorage for future page loads.
    // ----------------------------------------------------------------
    'start' => (function () use ($turso) {
        $body      = body();
        $widgetId  = $body['widget_id'] ?? '';
        $name      = trim($body['name']  ?? '');
        $phone     = trim($body['phone'] ?? '');
        $email     = trim($body['email'] ?? '');

        if ($widgetId === '') err('Missing widget_id');
        if ($name === '')     err('Name is required');

        // Look up workspace by widget_id
        $ws = $turso->fetchAll(
            "SELECT id FROM workspaces WHERE widget_id = ?", [$widgetId]
        );
        if (empty($ws)) err('Widget not found');

        $workspaceId = (int) $ws[0]['id'];
        $sessionId   = bin2hex(random_bytes(16)); // unique 32-char session token

        $turso->execute(
            "INSERT INTO chat_sessions
             (workspace_id, session_id, visitor_name, visitor_phone, visitor_email)
             VALUES (?, ?, ?, ?, ?)",
            [$workspaceId, $sessionId, $name, $phone, $email]
        );

        ok(['session_id' => $sessionId, 'name' => $name]);
    })(),

    // ----------------------------------------------------------------
    // POST ?action=save_message
    // Called after every user message and every assistant response.
    // Saves the message to Turso and updates last_active timestamp.
    // ----------------------------------------------------------------
    'save_message' => (function () use ($turso) {
        $body      = body();
        $sessionId = $body['session_id'] ?? '';
        $role      = $body['role']       ?? ''; // 'user' or 'assistant'
        $content   = $body['content']    ?? '';

        if ($sessionId === '') err('Missing session_id');
        if (!in_array($role, ['user', 'assistant'])) err('Invalid role');
        if ($content === '')   err('Empty content');

        $turso->execute(
            "INSERT INTO chat_messages (session_id, role, content) VALUES (?, ?, ?)",
            [$sessionId, $role, $content]
        );

        // Update session's last_active and message count
        $turso->execute(
            "UPDATE chat_sessions
             SET last_active = datetime('now'),
                 message_count = message_count + 1
             WHERE session_id = ?",
            [$sessionId]
        );

        ok(['saved' => true]);
    })(),

    // ----------------------------------------------------------------
    // GET ?action=load&session_id=xxx
    // Called on page load to restore conversation history.
    // Returns visitor info + all messages in order.
    // ----------------------------------------------------------------
    'load' => (function () use ($turso) {
        $sessionId = $_GET['session_id'] ?? '';
        if ($sessionId === '') err('Missing session_id');

        $session = $turso->fetchAll(
            "SELECT * FROM chat_sessions WHERE session_id = ?", [$sessionId]
        );
        if (empty($session)) err('Session not found');

        $messages = $turso->fetchAll(
            "SELECT role, content, created_at FROM chat_messages
             WHERE session_id = ? ORDER BY id ASC",
            [$sessionId]
        );

        ok([
            'session'  => $session[0],
            'messages' => $messages,
        ]);
    })(),

    default => err("Unknown action: $action")
};
