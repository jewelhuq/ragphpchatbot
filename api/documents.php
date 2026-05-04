<?php

require_once __DIR__ . '/bootstrap.php';

use RAG\Admin\DocumentManager;

$account = requireAuth($auth);
$action  = $_GET['action'] ?? 'list';
$wsId    = (int)($_GET['workspace_id'] ?? 0);

$docManager = new DocumentManager($turso, __DIR__ . '/../storage/workspaces');

try {
    match ($action) {

        // GET /api/documents.php?action=list&workspace_id=X
        'list' => (function () use ($docManager, $wsId) {
            json_ok($docManager->list($wsId));
        })(),

        // POST /api/documents.php?action=upload&workspace_id=X
        // multipart/form-data with file field 'document'
        'upload' => (function () use ($docManager, $wsId) {
            if (empty($_FILES['document'])) {
                json_error('No file uploaded.');
            }
            $doc = $docManager->upload($wsId, $_FILES['document']);
            json_ok($doc, 201);
        })(),

        // DELETE /api/documents.php?action=delete&workspace_id=X&id=Y
        'delete' => (function () use ($docManager, $wsId) {
            $docId = (int)($_GET['id'] ?? 0);
            $docManager->delete($docId, $wsId);
            json_ok(['message' => 'Document deleted.']);
        })(),

        default => json_error("Unknown action: $action")
    };
} catch (\Exception $e) {
    json_error($e->getMessage());
}
