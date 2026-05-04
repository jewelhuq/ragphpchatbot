<?php

require_once __DIR__ . '/bootstrap.php';

use RAG\Admin\WorkspaceManager;

$account = requireAuth($auth);
$manager = new WorkspaceManager($turso);
$action  = $_GET['action'] ?? 'list';

try {
    match ($action) {

        // GET /api/workspaces.php?action=list
        'list' => (function () use ($manager, $account) {
            $workspaces = $manager->listForAccount(
                (int)$account['id'],
                $account['role'],
                $account['owner_id'] ? (int)$account['owner_id'] : null
            );
            json_ok($workspaces);
        })(),

        // GET /api/workspaces.php?action=get&id=X
        'get' => (function () use ($manager, $account) {
            $workspace = $manager->get(
                (int)($_GET['id'] ?? 0),
                (int)$account['id'],
                $account['role'],
                $account['owner_id'] ? (int)$account['owner_id'] : null
            );
            json_ok($workspace);
        })(),

        // POST /api/workspaces.php?action=create
        'create' => (function () use ($manager, $account) {
            $body = requestBody();
            $ownerId = $account['role'] === 'member'
                ? (int)$account['owner_id']
                : (int)$account['id'];

            $workspace = $manager->create(
                $ownerId,
                $body['name']        ?? 'New Workspace',
                $body['description'] ?? ''
            );
            json_ok($workspace, 201);
        })(),

        // PUT /api/workspaces.php?action=update&id=X
        'update' => (function () use ($manager, $account) {
            $body = requestBody();
            $manager->update(
                (int)($_GET['id'] ?? 0),
                $body['name']        ?? '',
                $body['description'] ?? ''
            );
            json_ok(['message' => 'Workspace updated.']);
        })(),

        // PUT /api/workspaces.php?action=settings&id=X
        'settings' => (function () use ($manager, $account, $config) {
            $body = requestBody();
            $manager->updateSettings(
                (int)($_GET['id'] ?? 0),
                $body,
                $config['encryption_key']
            );
            json_ok(['message' => 'Settings saved.']);
        })(),

        // DELETE /api/workspaces.php?action=delete&id=X
        'delete' => (function () use ($manager) {
            $manager->delete((int)($_GET['id'] ?? 0));
            json_ok(['message' => 'Workspace deleted.']);
        })(),

        // GET /api/workspaces.php?action=models
        'models' => (function () {
            json_ok([
                'embedding' => WorkspaceManager::EMBEDDING_MODELS,
                'llm'       => WorkspaceManager::LLM_MODELS,
            ]);
        })(),

        default => json_error("Unknown action: $action")
    };
} catch (\Exception $e) {
    json_error($e->getMessage());
}
