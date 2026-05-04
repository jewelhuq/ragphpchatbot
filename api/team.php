<?php

require_once __DIR__ . '/bootstrap.php';

$account = requireAuth($auth);
$action  = $_GET['action'] ?? 'list';

// Only owners can manage team members
if ($account['role'] !== 'owner') {
    json_error('Only account owners can manage team members.', 403);
}

try {
    match ($action) {

        // GET /api/team.php?action=list
        'list' => (function () use ($turso, $account) {
            $members = $turso->fetchAll(
                "SELECT id, name, email, role, created_at
                 FROM accounts
                 WHERE account_id = ?
                 ORDER BY created_at DESC",
                [(int)$account['id']]
            );
            json_ok($members);
        })(),

        // POST /api/team.php?action=invite
        'invite' => (function () use ($auth, $account) {
            $body = requestBody();
            $member = $auth->addTeamMember(
                (int)$account['id'],
                $body['name']     ?? '',
                $body['email']    ?? '',
                $body['password'] ?? ''
            );
            json_ok($member, 201);
        })(),

        // DELETE /api/team.php?action=remove&id=X
        'remove' => (function () use ($turso, $account) {
            $memberId = (int)($_GET['id'] ?? 0);

            // Verify this member belongs to this owner
            $member = $turso->fetchAll(
                "SELECT id FROM accounts WHERE id = ? AND account_id = ?",
                [$memberId, (int)$account['id']]
            );

            if (empty($member)) {
                json_error('Team member not found.', 404);
            }

            $turso->execute("DELETE FROM sessions WHERE account_id = ?", [$memberId]);
            $turso->execute("DELETE FROM accounts WHERE id = ?", [$memberId]);
            json_ok(['message' => 'Team member removed.']);
        })(),

        default => json_error("Unknown action: $action")
    };
} catch (\Exception $e) {
    json_error($e->getMessage());
}
