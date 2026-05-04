<?php

// ============================================================
// widget_chat.php — Public SSE chat endpoint for the widget
// ============================================================
// Called by the embedded widget. No auth required — public.
// Looks up the workspace by widget_id, loads its settings,
// retrieves relevant chunks, streams the LLM response.
// ============================================================

require_once __DIR__ . '/../vendor/autoload.php';

use RAG\Turso;
use RAG\Ollama;
use RAG\VectorSearch;
use RAG\Prompt;
use RAG\Admin\WorkspaceManager;

$config = require __DIR__ . '/../config.php';

header('Content-Type: text/event-stream');
header('Cache-Control: no-cache');
header('X-Accel-Buffering: no');
header('Access-Control-Allow-Origin: *');

function sse(string $event, string $data): void
{
    echo "event: {$event}\n";
    echo "data: {$data}\n\n";
    flush();
}

$widgetId  = $_GET['widget_id']  ?? '';
$query     = trim($_GET['q']       ?? '');
$sessionId = $_GET['session_id']   ?? '';
$history   = json_decode($_GET['history'] ?? '[]', true) ?? [];

if ($widgetId === '' || $query === '') {
    sse('error', json_encode(['message' => 'Missing widget_id or query.']));
    sse('done', '');
    exit;
}

$turso = new Turso($config);

// ----------------------------------------------------------------
// Guard: if an admin has taken over this session, do not let the
// AI respond. The widget should have stopped sending here already,
// but this is a server-side safety net.
// ----------------------------------------------------------------
if ($sessionId !== '') {
    $sessionCheck = $turso->fetchAll(
        "SELECT mode FROM chat_sessions WHERE session_id = ?",
        [$sessionId]
    );
    if (!empty($sessionCheck) && $sessionCheck[0]['mode'] === 'human') {
        sse('error', json_encode(['message' => 'Session is handled by a live agent.']));
        sse('done', '');
        exit;
    }
}

// Look up workspace by widget_id
$workspace = $turso->fetchAll(
    "SELECT w.*, ws.llm_provider, ws.llm_model, ws.embedding_provider,
             ws.embedding_model, ws.embedding_dims, ws.api_keys
     FROM workspaces w
     LEFT JOIN workspace_settings ws ON ws.workspace_id = w.id
     WHERE w.widget_id = ?",
    [$widgetId]
);

if (empty($workspace)) {
    sse('error', json_encode(['message' => 'Widget not found.']));
    sse('done', '');
    exit;
}

$workspace = $workspace[0];
$wsId      = (int)$workspace['id'];

// Build merged settings: workspace overrides central config
$mergedSettings = array_merge($config, [
    'chat_model'      => $workspace['llm_model']       ?? $config['chat_model'],
    'embedding_model' => $workspace['embedding_model'] ?? $config['embedding_model'],
    'embedding_dims'  => (int)($workspace['embedding_dims'] ?? $config['embedding_dims']),
]);

$ollama = new Ollama($mergedSettings);
$search = new VectorSearch($turso, $ollama, $config['top_k'], $wsId);

// Retrieve relevant chunks scoped to this workspace
$chunks  = $search->search($query);
$sources = array_unique(array_column($chunks, 'source'));

// Build system prompt
$systemPrompt = \RAG\Prompt::build($chunks);

// Assemble conversation
$messages = [['role' => 'system', 'content' => $systemPrompt]];
foreach (array_slice($history, -10) as $turn) {
    $messages[] = $turn;
}
$messages[] = ['role' => 'user', 'content' => $query];

// Stream response and collect full answer for saving
$fullResponse = '';
$ollama->chatStream($messages, function (string $token) use (&$fullResponse) {
    $escaped = str_replace("\n", "\\n", $token);
    sse('token', $escaped);
    $fullResponse .= $token;
});

// Save both sides of this turn to Turso if session_id provided
if ($sessionId !== '' && $query !== '' && $fullResponse !== '') {
    $turso->execute(
        "INSERT INTO chat_messages (session_id, role, content) VALUES (?, ?, ?)",
        [$sessionId, 'user', $query]
    );
    $turso->execute(
        "INSERT INTO chat_messages (session_id, role, content) VALUES (?, ?, ?)",
        [$sessionId, 'assistant', $fullResponse]
    );
    $turso->execute(
        "UPDATE chat_sessions
         SET last_active = datetime('now'), message_count = message_count + 2
         WHERE session_id = ?",
        [$sessionId]
    );
}

sse('sources', json_encode(array_values($sources)));
sse('done', '');
