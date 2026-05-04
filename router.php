<?php

// ============================================================
// Admin Panel Router — entry point for all /admin/api/* requests
// ============================================================
// Apache (via .htaccess) rewrites clean URLs like:
//   GET /admin/api/health.php
//   POST /admin/api/workspaces.php?action=create
// directly to the matching file in admin/api/.
//
// This router acts as an optional front-controller. When PHP
// is running in CGI/CLI mode without Apache rewriting (e.g.
// during development with `php -S`), this file catches every
// request, resolves the target script, and includes it.
//
// In production under Apache the .htaccess rule is sufficient
// and this file is never invoked for normal API traffic — it
// sits here as a safety net and for local dev convenience.
// ============================================================

// ----------------------------------------------------------
// Set shared headers up front so they are emitted regardless
// of which code path handles the request.
// ----------------------------------------------------------
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');

// Preflight — browsers send this before any cross-origin request.
// Answer immediately so the real request is not blocked.
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

// ----------------------------------------------------------
// Resolve the requested path relative to this file.
// We expect URLs of the form:  /admin/api/<endpoint>.php
// ----------------------------------------------------------
$requestUri  = $_SERVER['REQUEST_URI'] ?? '/';
$scriptName  = $_SERVER['SCRIPT_NAME'] ?? '';

// Strip the query string so path matching is clean.
$path = parse_url($requestUri, PHP_URL_PATH);

// ----------------------------------------------------------
// Health check shortcut
// GET /admin/api/health.php → no auth, no DB, instant reply.
// Useful for load-balancers and uptime monitors.
// ----------------------------------------------------------
if ($path === '/admin/api/health.php' || $path === '/api/health.php') {
    echo json_encode([
        'ok'        => true,
        'status'    => 'healthy',
        'timestamp' => date('c'),
    ]);
    exit;
}

// ----------------------------------------------------------
// Dynamic dispatch
// Map the URL path to a real file under admin/api/.
// ----------------------------------------------------------
$apiDir = __DIR__ . '/api';

// Acceptable prefixes for the API base path.
$prefixes = ['/admin/api/', '/api/'];
$endpoint = null;

foreach ($prefixes as $prefix) {
    if (str_starts_with($path, $prefix)) {
        // Everything after the prefix is the endpoint filename.
        $relative = substr($path, strlen($prefix));
        $endpoint = $relative;
        break;
    }
}

if ($endpoint === null) {
    // The request does not belong to the admin API namespace.
    http_response_code(404);
    echo json_encode(['ok' => false, 'error' => 'Not found.']);
    exit;
}

// ----------------------------------------------------------
// Security: prevent path traversal attempts like ../../etc/passwd
// ----------------------------------------------------------
$endpoint = ltrim($endpoint, '/');

if (
    str_contains($endpoint, '..')  ||
    str_contains($endpoint, "\0") ||
    str_contains($endpoint, '\\')
) {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Invalid path.']);
    exit;
}

// Ensure the endpoint carries the .php extension.
if (!str_ends_with($endpoint, '.php')) {
    $endpoint .= '.php';
}

$target = realpath($apiDir . '/' . $endpoint);

// Double-check the resolved path is still inside admin/api/
// (realpath() collapses any remaining traversal tricks).
if ($target === false || !str_starts_with($target, realpath($apiDir))) {
    http_response_code(404);
    echo json_encode(['ok' => false, 'error' => 'Endpoint not found.']);
    exit;
}

// Hand off to the endpoint file — it inherits this scope.
require $target;
