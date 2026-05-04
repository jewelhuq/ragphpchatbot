<?php

// ============================================================
// widget_faq.php — Public FAQ endpoint for the widget
// ============================================================
// Called by the embedded widget to populate its FAQ tab.
// No authentication required — this is a public endpoint.
//
// GET params:
//   widget_id  (required) — identifies the workspace
//   q          (optional) — fuzzy search query
//
// Returns:
//   {"ok": true,  "data": [...faq items...]}
//   {"ok": false, "error": "Widget not found"} on 404
// ============================================================

require_once __DIR__ . '/../vendor/autoload.php';

use RAG\Turso;
use RAG\Admin\FaqManager;

$config = require __DIR__ . '/../config.php';

// ----------------------------------------------------------
// Step 1: Announce ourselves to the world.
// We're a public endpoint, so any origin may call us,
// and we always speak JSON.
// ----------------------------------------------------------
header('Access-Control-Allow-Origin: *');
header('Content-Type: application/json');

// Handle CORS preflight requests gracefully
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

// ----------------------------------------------------------
// Step 2: Read and validate the incoming GET parameters.
// widget_id is the key that ties a public widget to a
// workspace — without it we have nothing to look up.
// ----------------------------------------------------------
$widgetId    = trim($_GET['widget_id'] ?? '');
$searchQuery = trim($_GET['q']         ?? '');

if ($widgetId === '') {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Missing widget_id.']);
    exit;
}

// ----------------------------------------------------------
// Step 3: Connect to the database and look up the workspace
// that owns this widget_id. We only need the workspace id —
// there's no need to JOIN workspace_settings here.
// ----------------------------------------------------------
$turso = new Turso($config);

$rows = $turso->fetchAll(
    "SELECT id FROM workspaces WHERE widget_id = ? LIMIT 1",
    [$widgetId]
);

if (empty($rows)) {
    // The widget_id doesn't map to any known workspace.
    // Return a clean 404 so the widget can fail gracefully.
    http_response_code(404);
    echo json_encode(['ok' => false, 'error' => 'Widget not found.']);
    exit;
}

$workspaceId = (int)$rows[0]['id'];

// ----------------------------------------------------------
// Step 4: Fetch the FAQ items.
// If the caller sent a search query we run a fuzzy search
// across question text; otherwise we return the full list
// in their configured sort order.
// ----------------------------------------------------------
$faqManager = new FaqManager($turso);

if ($searchQuery !== '') {
    $items = $faqManager->search($workspaceId, $searchQuery);
} else {
    $items = $faqManager->list($workspaceId);
}

// ----------------------------------------------------------
// Step 5: Strip internal scoring fields before sending data
// to the public. The _score key is only meaningful server-
// side (used by FaqManager to sort search results) and
// should never leak into the public API response.
// ----------------------------------------------------------
$publicItems = array_map(function (array $item): array {
    unset($item['_score']);
    return $item;
}, $items);

// ----------------------------------------------------------
// Step 6: Send the response. Always an array under "data",
// even when there are zero FAQ items — the widget can simply
// show an empty state without any special-casing.
// ----------------------------------------------------------
echo json_encode(['ok' => true, 'data' => array_values($publicItems)]);
