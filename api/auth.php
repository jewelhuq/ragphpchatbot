<?php

require_once __DIR__ . '/bootstrap.php';

use RAG\Admin\Auth;

$action = $_GET['action'] ?? '';

try {
    match ($action) {

        // POST /api/auth.php?action=signup
        'signup' => (function () use ($auth) {
            $body = requestBody();
            $result = $auth->signup(
                $body['name']     ?? '',
                $body['email']    ?? '',
                $body['password'] ?? ''
            );
            json_ok($result, 201);
        })(),

        // POST /api/auth.php?action=login
        'login' => (function () use ($auth) {
            $body = requestBody();
            $result = $auth->login(
                $body['email']    ?? '',
                $body['password'] ?? ''
            );
            json_ok($result);
        })(),

        // POST /api/auth.php?action=logout
        'logout' => (function () use ($auth) {
            $header = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
            $token  = str_starts_with($header, 'Bearer ') ? substr($header, 7) : '';
            if ($token) $auth->logout($token);
            json_ok(['message' => 'Logged out.']);
        })(),

        // POST /api/auth.php?action=change-password
        'change-password' => (function () use ($auth) {
            $account = requireAuth($auth);
            $body    = requestBody();
            $auth->changePassword(
                (int)$account['id'],
                $body['current_password'] ?? '',
                $body['new_password']     ?? ''
            );
            json_ok(['message' => 'Password updated.']);
        })(),

        // GET /api/auth.php?action=me
        'me' => (function () use ($auth) {
            $account = requireAuth($auth);
            json_ok($account);
        })(),

        default => json_error("Unknown action: $action")
    };
} catch (\InvalidArgumentException $e) {
    json_error($e->getMessage(), 422);
} catch (\RuntimeException $e) {
    json_error($e->getMessage(), 400);
} catch (\Exception $e) {
    json_error("Server error: " . $e->getMessage(), 500);
}
