<?php

require_once __DIR__ . '/bootstrap.php';

use RAG\Admin\FaqManager;

$account    = requireAuth($auth);
$action     = $_GET['action'] ?? 'list';
$wsId       = (int)($_GET['workspace_id'] ?? 0);
$faqManager = new FaqManager($turso);

try {
    match ($action) {

        // GET /api/faq.php?action=list&workspace_id=X
        'list' => (function () use ($faqManager, $wsId) {
            json_ok($faqManager->list($wsId));
        })(),

        // GET /api/faq.php?action=search&workspace_id=X&q=...
        'search' => (function () use ($faqManager, $wsId) {
            $query = $_GET['q'] ?? '';
            json_ok($faqManager->search($wsId, $query));
        })(),

        // POST /api/faq.php?action=create&workspace_id=X
        'create' => (function () use ($faqManager, $wsId) {
            $body = requestBody();
            $item = $faqManager->create(
                $wsId,
                $body['question'] ?? '',
                $body['answer']   ?? ''
            );
            json_ok($item, 201);
        })(),

        // PUT /api/faq.php?action=update&workspace_id=X&id=Y
        'update' => (function () use ($faqManager, $wsId) {
            $body = requestBody();
            $faqManager->update(
                (int)($_GET['id'] ?? 0),
                $wsId,
                $body['question'] ?? '',
                $body['answer']   ?? ''
            );
            json_ok(['message' => 'FAQ updated.']);
        })(),

        // DELETE /api/faq.php?action=delete&workspace_id=X&id=Y
        'delete' => (function () use ($faqManager, $wsId) {
            $faqManager->delete((int)($_GET['id'] ?? 0), $wsId);
            json_ok(['message' => 'FAQ deleted.']);
        })(),

        // POST /api/faq.php?action=reorder&workspace_id=X
        // body: { ids: [3, 1, 2] }
        'reorder' => (function () use ($faqManager, $wsId) {
            $body = requestBody();
            $faqManager->reorder($wsId, $body['ids'] ?? []);
            json_ok(['message' => 'Order saved.']);
        })(),

        default => json_error("Unknown action: $action")
    };
} catch (\Exception $e) {
    json_error($e->getMessage());
}
