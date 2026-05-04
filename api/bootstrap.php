<?php

// ============================================================
// API Bootstrap — shared setup for all admin API endpoints
// ============================================================
// Every API file includes this. It:
//   1. Loads autoloader and config
//   2. Handles CORS for the admin SPA
//   3. Provides helpers: json_ok(), json_error(), requireAuth()
//   4. Authenticates the request from the Bearer token
// ============================================================

require_once __DIR__ . '/../vendor/autoload.php';

use RAG\Turso;
use RAG\Admin\Auth;
use RAG\Admin\Database;

$config = require __DIR__ . '/../config.php';

// CORS — allow the admin frontend to call the API
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

// Bootstrap the database tables on every request (safe — IF NOT EXISTS)
$turso = new Turso($config);
(new Database($turso))->migrate();

$auth = new Auth($turso);

// ----------------------------------------------------------
// Send a JSON success response and exit.
// ----------------------------------------------------------
function json_ok(mixed $data = null, int $code = 200): never
{
    http_response_code($code);
    echo json_encode(['ok' => true, 'data' => $data]);
    exit;
}

// ----------------------------------------------------------
// Send a JSON error response and exit.
// ----------------------------------------------------------
function json_error(string $message, int $code = 400): never
{
    http_response_code($code);
    echo json_encode(['ok' => false, 'error' => $message]);
    exit;
}

// ----------------------------------------------------------
// Extract and validate the Bearer token from Authorization header.
// Returns the authenticated account row or calls json_error().
// ----------------------------------------------------------
function requireAuth(Auth $auth): array
{
    $header = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    $token  = str_starts_with($header, 'Bearer ') ? substr($header, 7) : '';

    if ($token === '') {
        json_error('Unauthorized.', 401);
    }

    try {
        return $auth->authenticate($token);
    } catch (\Exception $e) {
        json_error($e->getMessage(), 401);
    }
}

// ----------------------------------------------------------
// Get request body as decoded JSON array.
// ----------------------------------------------------------
function requestBody(): array
{
    $raw = file_get_contents('php://input');
    return json_decode($raw, true) ?? [];
}
