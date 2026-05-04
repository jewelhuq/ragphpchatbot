<?php

// ============================================================
// Global Settings Endpoint
// ============================================================
// Exposes — and in the future may update — the server-level
// configuration values that affect the whole RAG system:
//   - ollama_url      : where the local Ollama server lives
//   - chat_model      : which LLM generates answers
//   - embedding_model : which model turns text into vectors
//
// These values are currently defined in config.php on the
// server. Reading them is safe for any authenticated owner.
// Writing (POST action=update) is acknowledged but not yet
// persisted — callers must edit config.php directly for now.
//
// Routes
//   GET  /admin/api/global_settings.php?action=get    → returns current values
//   POST /admin/api/global_settings.php?action=update → no-op with guidance
//
// Auth: owner role required for all actions.
// ============================================================

require_once __DIR__ . '/bootstrap.php';

// ----------------------------------------------------------
// Gate: only account owners may read or change global settings.
// Members and guests have no business seeing server-level URLs
// or model names that could leak infrastructure details.
// ----------------------------------------------------------
$account = requireAuth($auth);

if ($account['role'] !== 'owner') {
    json_error('Only account owners can access global settings.', 403);
}

$action = $_GET['action'] ?? 'get';

try {
    match ($action) {

        // GET /admin/api/global_settings.php?action=get
        // --------------------------------------------------
        // Return the three Ollama-related settings from config.
        // We intentionally expose only these fields — secrets
        // like turso_token and encryption_key never leave the
        // server through this endpoint.
        // --------------------------------------------------
        'get' => (function () use ($config) {
            json_ok([
                'ollama_url'      => $config['ollama_url']      ?? 'http://127.0.0.1:11434',
                'chat_model'      => $config['chat_model']      ?? 'gemma3:1b',
                'embedding_model' => $config['embedding_model'] ?? 'nomic-embed-text',
            ]);
        })(),

        // POST /admin/api/global_settings.php?action=update
        // --------------------------------------------------
        // config.php lives on the server filesystem and is not
        // writable at runtime (nor should it be — overwriting a
        // PHP file via an HTTP request is a security risk).
        //
        // For now we acknowledge the request and tell the caller
        // what to do. A future iteration could write to a
        // database-backed settings table and merge with config.
        // --------------------------------------------------
        'update' => (function () {
            json_ok(['message' => 'To persist, edit config.php on the server.']);
        })(),

        default => json_error("Unknown action: $action")
    };
} catch (\Exception $e) {
    json_error($e->getMessage());
}
