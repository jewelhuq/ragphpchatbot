<?php

namespace RAG\Admin;

use RAG\Turso;

// ============================================================
// WorkspaceManager — CRUD for Workspaces and their Settings
// ============================================================
// Each workspace is an isolated RAG environment with:
//   - Its own documents and chunks (filtered by workspace_id)
//   - Its own LLM and embedding model settings
//   - Its own FAQ items
//   - A unique widget_id for the public embed script
//   - Optional custom Turso URL (their own DB)
// ============================================================

class WorkspaceManager
{
    private Turso $turso;

    // Available embedding models with quality/cost metadata
    public const EMBEDDING_MODELS = [
        'ollama:nomic-embed-text' => [
            'label'    => 'Local — nomic-embed-text',
            'dims'     => 768,
            'tier'     => 'free',
            'badge'    => '🟢 Free',
            'note'     => 'Runs locally. No API key needed. Good quality.',
            'provider' => 'ollama',
            'model'    => 'nomic-embed-text',
        ],
        'ollama:mxbai-embed-large' => [
            'label'    => 'Local — mxbai-embed-large',
            'dims'     => 1024,
            'tier'     => 'free',
            'badge'    => '🟢 Free+',
            'note'     => 'Best free local embedding. Better recall than nomic.',
            'provider' => 'ollama',
            'model'    => 'mxbai-embed-large',
        ],
        'openai:text-embedding-3-small' => [
            'label'    => 'OpenAI — text-embedding-3-small',
            'dims'     => 1536,
            'tier'     => 'cheap',
            'badge'    => '🟡 Cheap',
            'note'     => '$0.02/1M tokens. Great quality, very affordable.',
            'provider' => 'openai',
            'model'    => 'text-embedding-3-small',
        ],
        'openai:text-embedding-3-large' => [
            'label'    => 'OpenAI — text-embedding-3-large',
            'dims'     => 3072,
            'tier'     => 'premium',
            'badge'    => '🔵 Premium',
            'note'     => '$0.13/1M tokens. Highest accuracy for complex docs.',
            'provider' => 'openai',
            'model'    => 'text-embedding-3-large',
        ],
        'voyage:voyage-3' => [
            'label'    => 'Voyage AI — voyage-3',
            'dims'     => 1024,
            'tier'     => 'best',
            'badge'    => '🔴 Best for RAG',
            'note'     => '$0.06/1M tokens. Purpose-built for RAG retrieval.',
            'provider' => 'voyage',
            'model'    => 'voyage-3',
        ],
    ];

    // Available LLM models with quality/cost metadata
    public const LLM_MODELS = [
        'ollama:gemma3:1b' => [
            'label'    => 'Local — Gemma 3 1B',
            'badge'    => '🟢 Free',
            'note'     => 'Runs locally. Free but slow on CPU. Good for testing.',
            'provider' => 'ollama',
            'model'    => 'gemma3:1b',
        ],
        'ollama:gemma3:4b' => [
            'label'    => 'Local — Gemma 3 4B',
            'badge'    => '🟢 Free+',
            'note'     => 'Runs locally. Better quality, needs 8GB+ RAM.',
            'provider' => 'ollama',
            'model'    => 'gemma3:4b',
        ],
        'openai:gpt-4o-mini' => [
            'label'    => 'OpenAI — GPT-4o mini',
            'badge'    => '🟡 Cheap',
            'note'     => '~$0.15/1M tokens. Fast, affordable, good quality.',
            'provider' => 'openai',
            'model'    => 'gpt-4o-mini',
        ],
        'openai:gpt-4o-turbo' => [
            'label'    => 'OpenAI — GPT-4o Turbo',
            'badge'    => '🟡 Balanced',
            'note'     => '~$1/1M tokens. Fast, strong instruction following.',
            'provider' => 'openai',
            'model'    => 'gpt-4o-turbo',
        ],
        'openai:gpt-4o' => [
            'label'    => 'OpenAI — GPT-4o',
            'badge'    => '🔴 Top Quality',
            'note'     => '~$2.50/1M tokens. OpenAI flagship model.',
            'provider' => 'openai',
            'model'    => 'gpt-4o',
        ],
    ];

    public function __construct(Turso $turso)
    {
        $this->turso = $turso;
    }

    // ----------------------------------------------------------
    // Create a new workspace for an account.
    // Generates a unique widget_id and default settings.
    // ----------------------------------------------------------
    public function create(int $accountId, string $name, string $description = ''): array
    {
        $widgetId = 'ws_' . bin2hex(random_bytes(12));

        $this->turso->execute(
            "INSERT INTO workspaces (account_id, name, description, widget_id)
             VALUES (?, ?, ?, ?)",
            [$accountId, $name, $description, $widgetId]
        );

        $workspace = $this->turso->fetchAll(
            "SELECT * FROM workspaces WHERE widget_id = ?", [$widgetId]
        )[0];

        // Create default settings for this workspace
        $this->turso->execute(
            "INSERT INTO workspace_settings (workspace_id) VALUES (?)",
            [$workspace['id']]
        );

        return $workspace;
    }

    // ----------------------------------------------------------
    // Get all workspaces for an account.
    // Members see workspaces of their owner's account.
    // ----------------------------------------------------------
    public function listForAccount(int $accountId, string $role, ?int $ownerId): array
    {
        $resolvedAccountId = ($role === 'member' && $ownerId) ? $ownerId : $accountId;

        return $this->turso->fetchAll(
            "SELECT w.*,
                    (SELECT COUNT(*) FROM documents d WHERE d.workspace_id = w.id AND d.status = 'done') as doc_count,
                    (SELECT COUNT(*) FROM faq f WHERE f.workspace_id = w.id) as faq_count
             FROM workspaces w
             WHERE w.account_id = ?
             ORDER BY w.created_at DESC",
            [$resolvedAccountId]
        );
    }

    // ----------------------------------------------------------
    // Get a single workspace — verifies ownership.
    // ----------------------------------------------------------
    public function get(int $workspaceId, int $accountId, string $role, ?int $ownerId): array
    {
        $resolvedAccountId = ($role === 'member' && $ownerId) ? $ownerId : $accountId;

        $workspace = $this->turso->fetchAll(
            "SELECT w.*, ws.llm_provider, ws.llm_model, ws.embedding_provider,
                    ws.embedding_model, ws.embedding_dims, ws.api_keys
             FROM workspaces w
             LEFT JOIN workspace_settings ws ON ws.workspace_id = w.id
             WHERE w.id = ? AND w.account_id = ?",
            [$workspaceId, $resolvedAccountId]
        );

        if (empty($workspace)) {
            throw new \RuntimeException("Workspace not found.");
        }

        return $workspace[0];
    }

    // ----------------------------------------------------------
    // Update workspace name/description.
    // ----------------------------------------------------------
    public function update(int $workspaceId, string $name, string $description): void
    {
        $this->turso->execute(
            "UPDATE workspaces SET name = ?, description = ? WHERE id = ?",
            [$name, $description, $workspaceId]
        );
    }

    // ----------------------------------------------------------
    // Update workspace LLM + embedding settings.
    // API keys encrypted before storing.
    // ----------------------------------------------------------
    public function updateSettings(int $workspaceId, array $settings, string $encryptionKey): void
    {
        $modelKey       = $settings['embedding_model_key'] ?? 'ollama:nomic-embed-text';
        $embeddingMeta  = self::EMBEDDING_MODELS[$modelKey] ?? self::EMBEDDING_MODELS['ollama:nomic-embed-text'];

        $llmKey     = $settings['llm_model_key'] ?? 'ollama:gemma3:1b';
        $llmMeta    = self::LLM_MODELS[$llmKey] ?? self::LLM_MODELS['ollama:gemma3:1b'];

        $apiKeys    = [
            'openai' => $settings['openai_key'] ?? '',
            'voyage' => $settings['voyage_key'] ?? '',
        ];

        $encryptedKeys = $this->encryptApiKeys($apiKeys, $encryptionKey);

        $this->turso->execute(
            "UPDATE workspace_settings SET
                llm_provider = ?, llm_model = ?,
                embedding_provider = ?, embedding_model = ?, embedding_dims = ?,
                api_keys = ?, updated_at = datetime('now')
             WHERE workspace_id = ?",
            [
                $llmMeta['provider'], $llmMeta['model'],
                $embeddingMeta['provider'], $embeddingMeta['model'], $embeddingMeta['dims'],
                $encryptedKeys,
                $workspaceId,
            ]
        );
    }

    // ----------------------------------------------------------
    // Delete a workspace and all its data.
    // ----------------------------------------------------------
    public function delete(int $workspaceId): void
    {
        $this->turso->execute("DELETE FROM chunks WHERE workspace_id = ?", [$workspaceId]);
        $this->turso->execute("DELETE FROM documents WHERE workspace_id = ?", [$workspaceId]);
        $this->turso->execute("DELETE FROM faq WHERE workspace_id = ?", [$workspaceId]);
        $this->turso->execute("DELETE FROM workspace_settings WHERE workspace_id = ?", [$workspaceId]);
        $this->turso->execute("DELETE FROM workspaces WHERE id = ?", [$workspaceId]);
    }

    // ----------------------------------------------------------
    // Encrypt/decrypt API keys using openssl_encrypt.
    // Key derived from the app encryption key in config.
    // ----------------------------------------------------------
    public function encryptApiKeys(array $keys, string $encryptionKey): string
    {
        $json  = json_encode($keys);
        $iv    = random_bytes(16);
        $encrypted = openssl_encrypt($json, 'AES-256-CBC', $encryptionKey, 0, $iv);
        return base64_encode($iv . '::' . $encrypted);
    }

    public function decryptApiKeys(string $encrypted, string $encryptionKey): array
    {
        try {
            $decoded = base64_decode($encrypted);
            [$iv, $data] = explode('::', $decoded, 2);
            $json = openssl_decrypt($data, 'AES-256-CBC', $encryptionKey, 0, $iv);
            return json_decode($json, true) ?? [];
        } catch (\Throwable $e) {
            return [];
        }
    }
}
