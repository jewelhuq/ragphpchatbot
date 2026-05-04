<?php

// ============================================================
// ingest.php — SSE endpoint for real-time ingest progress
// ============================================================
// Called after upload. Streams progress events back to the
// admin UI so they see: "Embedding chunk 3/12..." in real time.
// ============================================================

require_once __DIR__ . '/bootstrap.php';

use RAG\Admin\DocumentManager;
use RAG\Admin\WorkspaceManager;

// SSE headers
header('Content-Type: text/event-stream');
header('Cache-Control: no-cache');
header('X-Accel-Buffering: no');

function sseProgress(string $message, string $event = 'progress'): void
{
    echo "event: {$event}\n";
    echo "data: " . json_encode(['message' => $message]) . "\n\n";
    flush();
}

$token = str_replace('Bearer ', '', $_SERVER['HTTP_AUTHORIZATION'] ?? '');
if (!$token) $token = $_GET['token'] ?? '';

try {
    $account = $auth->authenticate($token);
} catch (\Exception $e) {
    sseProgress('Unauthorized.', 'error');
    sseProgress('', 'done');
    exit;
}

$docId = (int)($_GET['doc_id']      ?? 0);
$wsId  = (int)($_GET['workspace_id'] ?? 0);

// Load workspace settings to get the right embedding model/keys
$wsManager  = new WorkspaceManager($turso);
$workspace  = $turso->fetchAll(
    "SELECT ws.* FROM workspace_settings ws WHERE ws.workspace_id = ?",
    [$wsId]
);

$settings = !empty($workspace) ? $workspace[0] : [];

// Merge central config with workspace-level overrides
$mergedSettings = array_merge($config, [
    'chat_model'      => $settings['llm_model']       ?? $config['chat_model'],
    'embedding_model' => $settings['embedding_model'] ?? $config['embedding_model'],
    'embedding_dims'  => (int)($settings['embedding_dims'] ?? $config['embedding_dims']),
]);

$docManager = new DocumentManager($turso, __DIR__ . '/../storage/workspaces');

try {
    $docManager->ingest(
        $docId,
        $wsId,
        $mergedSettings,
        fn(string $msg) => sseProgress($msg)
    );
    sseProgress('Ingestion complete!', 'done');
} catch (\Exception $e) {
    sseProgress('Error: ' . $e->getMessage(), 'error');
    sseProgress('', 'done');
}
