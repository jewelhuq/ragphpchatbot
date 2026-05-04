<?php

namespace RAG\Admin;

use RAG\Turso;

// ============================================================
// Auth — Signup, Login, Sessions, Password Management
// ============================================================
// All auth is token-based. On login we generate a random token,
// store it in the sessions table, and return it to the client.
// The client stores it in localStorage and sends it as a header
// on every API request: Authorization: Bearer <token>
// ============================================================

class Auth
{
    private Turso $turso;

    public function __construct(Turso $turso)
    {
        $this->turso = $turso;
    }

    // ----------------------------------------------------------
    // Register a new account owner.
    // The first user to sign up with a given email becomes an
    // 'owner' — they can then invite team members.
    // ----------------------------------------------------------
    public function signup(string $name, string $email, string $password): array
    {
        $email = strtolower(trim($email));

        if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
            throw new \InvalidArgumentException("Invalid email address.");
        }

        if (strlen($password) < 8) {
            throw new \InvalidArgumentException("Password must be at least 8 characters.");
        }

        $existing = $this->turso->fetchAll(
            "SELECT id FROM accounts WHERE email = ?", [$email]
        );

        if (!empty($existing)) {
            throw new \RuntimeException("An account with this email already exists.");
        }

        $hashed = password_hash($password, PASSWORD_BCRYPT);

        $this->turso->execute(
            "INSERT INTO accounts (name, email, password, role, account_id)
             VALUES (?, ?, ?, 'owner', NULL)",
            [$name, $email, $hashed]
        );

        $account = $this->turso->fetchAll(
            "SELECT id, name, email, role FROM accounts WHERE email = ?", [$email]
        )[0];

        return $this->issueSession($account);
    }

    // ----------------------------------------------------------
    // Log in with email and password.
    // Returns account data + session token.
    // ----------------------------------------------------------
    public function login(string $email, string $password): array
    {
        $email   = strtolower(trim($email));
        $account = $this->turso->fetchAll(
            "SELECT id, name, email, password, role, account_id
             FROM accounts WHERE email = ?",
            [$email]
        );

        if (empty($account)) {
            throw new \RuntimeException("Invalid email or password.");
        }

        $account = $account[0];

        if (!password_verify($password, $account['password'])) {
            throw new \RuntimeException("Invalid email or password.");
        }

        unset($account['password']);

        return $this->issueSession($account);
    }

    // ----------------------------------------------------------
    // Validate a session token from the Authorization header.
    // Returns the account row or throws if invalid/expired.
    // ----------------------------------------------------------
    public function authenticate(string $token): array
    {
        $session = $this->turso->fetchAll(
            "SELECT s.account_id, s.expires_at,
                    a.id, a.name, a.email, a.role, a.account_id as owner_id
             FROM sessions s
             JOIN accounts a ON a.id = s.account_id
             WHERE s.token = ?",
            [$token]
        );

        if (empty($session)) {
            throw new \RuntimeException("Invalid or expired session.");
        }

        $session = $session[0];

        if (strtotime($session['expires_at']) < time()) {
            $this->turso->execute("DELETE FROM sessions WHERE token = ?", [$token]);
            throw new \RuntimeException("Session expired. Please log in again.");
        }

        return $session;
    }

    // ----------------------------------------------------------
    // Add a team member to an owner's account.
    // Only owners can invite members.
    // ----------------------------------------------------------
    public function addTeamMember(int $ownerId, string $name, string $email, string $password): array
    {
        $email = strtolower(trim($email));

        $existing = $this->turso->fetchAll(
            "SELECT id FROM accounts WHERE email = ?", [$email]
        );

        if (!empty($existing)) {
            throw new \RuntimeException("A user with this email already exists.");
        }

        $hashed = password_hash($password, PASSWORD_BCRYPT);

        $this->turso->execute(
            "INSERT INTO accounts (name, email, password, role, account_id)
             VALUES (?, ?, ?, 'member', ?)",
            [$name, $email, $hashed, $ownerId]
        );

        return $this->turso->fetchAll(
            "SELECT id, name, email, role, account_id FROM accounts WHERE email = ?",
            [$email]
        )[0];
    }

    // ----------------------------------------------------------
    // Change password — user must provide their current password.
    // ----------------------------------------------------------
    public function changePassword(int $accountId, string $currentPassword, string $newPassword): void
    {
        if (strlen($newPassword) < 8) {
            throw new \InvalidArgumentException("New password must be at least 8 characters.");
        }

        $account = $this->turso->fetchAll(
            "SELECT password FROM accounts WHERE id = ?", [$accountId]
        );

        if (empty($account) || !password_verify($currentPassword, $account[0]['password'])) {
            throw new \RuntimeException("Current password is incorrect.");
        }

        $hashed = password_hash($newPassword, PASSWORD_BCRYPT);

        $this->turso->execute(
            "UPDATE accounts SET password = ? WHERE id = ?",
            [$hashed, $accountId]
        );
    }

    // ----------------------------------------------------------
    // Logout — delete the session token from the database.
    // ----------------------------------------------------------
    public function logout(string $token): void
    {
        $this->turso->execute("DELETE FROM sessions WHERE token = ?", [$token]);
    }

    // ----------------------------------------------------------
    // Generate a random session token valid for 30 days.
    // ----------------------------------------------------------
    private function issueSession(array $account): array
    {
        $token     = bin2hex(random_bytes(32));
        $expiresAt = date('Y-m-d H:i:s', strtotime('+30 days'));

        $this->turso->execute(
            "INSERT INTO sessions (account_id, token, expires_at) VALUES (?, ?, ?)",
            [$account['id'], $token, $expiresAt]
        );

        return [
            'token'   => $token,
            'account' => $account,
        ];
    }
}
