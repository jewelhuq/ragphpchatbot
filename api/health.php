<?php

// ============================================================
// Health Check Endpoint
// ============================================================
// GET /admin/api/health.php
//
// No authentication required — this endpoint exists so that
// load-balancers, uptime monitors, and CI pipelines can verify
// the server is alive without needing a valid session token.
//
// Response is always:
//   { "ok": true, "status": "healthy", "timestamp": "<ISO-8601>" }
// ============================================================

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

echo json_encode([
    'ok'        => true,
    'status'    => 'healthy',
    'timestamp' => date('c'),
]);
