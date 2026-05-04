<?php

namespace RAG\Admin;

use RAG\Turso;

// ============================================================
// Database — Admin Schema Bootstrap
// ============================================================
// Creates all tables needed for the admin panel on first run.
// Safe to call on every boot — uses CREATE TABLE IF NOT EXISTS.
//
// Tables:
//   accounts          — registered users (owners + team members)
//   workspaces        — each account's RAG workspaces
//   workspace_settings— per-workspace LLM/embedding config
//   documents         — uploaded files per workspace
//   faq               — manual Q&A per workspace
//   sessions          — auth session tokens
// ============================================================

class Database
{
    private Turso $turso;

    public function __construct(Turso $turso)
    {
        $this->turso = $turso;
    }

    public function migrate(): void
    {
        $this->createAccountsTable();
        $this->createWorkspacesTable();
        $this->createWorkspaceSettingsTable();
        $this->createDocumentsTable();
        $this->createFaqTable();
        $this->createSessionsTable();
    }

    // ----------------------------------------------------------
    // accounts — one row per user
    // role: 'owner' (signed up) | 'member' (invited by owner)
    // account_id: null for owners, points to owner for members
    // ----------------------------------------------------------
    private function createAccountsTable(): void
    {
        $this->turso->execute("
            CREATE TABLE IF NOT EXISTS accounts (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                account_id INTEGER,
                name       TEXT    NOT NULL,
                email      TEXT    NOT NULL UNIQUE,
                password   TEXT    NOT NULL,
                role       TEXT    NOT NULL DEFAULT 'owner',
                created_at TEXT    NOT NULL DEFAULT (datetime('now'))
            )
        ");
    }

    // ----------------------------------------------------------
    // workspaces — each workspace belongs to one account (owner)
    // widget_id: unique public ID used in the embed script
    // custom_turso_url: optional, if they want their own DB
    // ----------------------------------------------------------
    private function createWorkspacesTable(): void
    {
        $this->turso->execute("
            CREATE TABLE IF NOT EXISTS workspaces (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                account_id      INTEGER NOT NULL,
                name            TEXT    NOT NULL,
                description     TEXT    NOT NULL DEFAULT '',
                widget_id       TEXT    NOT NULL UNIQUE,
                custom_turso_url  TEXT  NOT NULL DEFAULT '',
                custom_turso_token TEXT NOT NULL DEFAULT '',
                created_at      TEXT    NOT NULL DEFAULT (datetime('now'))
            )
        ");
    }

    // ----------------------------------------------------------
    // workspace_settings — LLM + embedding config per workspace
    // api_keys stored as encrypted JSON blob
    // Falls back to central keys if workspace keys are empty
    // ----------------------------------------------------------
    private function createWorkspaceSettingsTable(): void
    {
        $this->turso->execute("
            CREATE TABLE IF NOT EXISTS workspace_settings (
                workspace_id     INTEGER PRIMARY KEY,
                llm_provider     TEXT NOT NULL DEFAULT 'ollama',
                llm_model        TEXT NOT NULL DEFAULT 'gemma3:1b',
                embedding_provider TEXT NOT NULL DEFAULT 'ollama',
                embedding_model  TEXT NOT NULL DEFAULT 'nomic-embed-text',
                embedding_dims   INTEGER NOT NULL DEFAULT 768,
                api_keys         TEXT NOT NULL DEFAULT '{}',
                updated_at       TEXT NOT NULL DEFAULT (datetime('now'))
            )
        ");
    }

    // ----------------------------------------------------------
    // documents — one row per uploaded file per workspace
    // status: 'pending' | 'ingesting' | 'done' | 'error'
    // ----------------------------------------------------------
    private function createDocumentsTable(): void
    {
        $this->turso->execute("
            CREATE TABLE IF NOT EXISTS documents (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                workspace_id INTEGER NOT NULL,
                filename     TEXT    NOT NULL,
                original_name TEXT   NOT NULL,
                file_hash    TEXT    NOT NULL,
                file_size    INTEGER NOT NULL DEFAULT 0,
                chunk_count  INTEGER NOT NULL DEFAULT 0,
                status       TEXT    NOT NULL DEFAULT 'pending',
                error        TEXT    NOT NULL DEFAULT '',
                created_at   TEXT    NOT NULL DEFAULT (datetime('now'))
            )
        ");
        $this->turso->execute("
            CREATE INDEX IF NOT EXISTS idx_documents_workspace
            ON documents(workspace_id)
        ");
    }

    // ----------------------------------------------------------
    // faq — manual Q&A pairs per workspace
    // answer stored as markdown (rendered in widget)
    // sort_order controls display order
    // ----------------------------------------------------------
    private function createFaqTable(): void
    {
        $this->turso->execute("
            CREATE TABLE IF NOT EXISTS faq (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                workspace_id INTEGER NOT NULL,
                question     TEXT    NOT NULL,
                answer       TEXT    NOT NULL,
                sort_order   INTEGER NOT NULL DEFAULT 0,
                created_at   TEXT    NOT NULL DEFAULT (datetime('now')),
                updated_at   TEXT    NOT NULL DEFAULT (datetime('now'))
            )
        ");
        $this->turso->execute("
            CREATE INDEX IF NOT EXISTS idx_faq_workspace
            ON faq(workspace_id)
        ");
    }

    // ----------------------------------------------------------
    // sessions — token-based auth (no cookies with samesite issues)
    // token: random 64-char hex stored in localStorage on client
    // ----------------------------------------------------------
    private function createSessionsTable(): void
    {
        $this->turso->execute("
            CREATE TABLE IF NOT EXISTS sessions (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                account_id INTEGER NOT NULL,
                token      TEXT    NOT NULL UNIQUE,
                expires_at TEXT    NOT NULL,
                created_at TEXT    NOT NULL DEFAULT (datetime('now'))
            )
        ");
        $this->turso->execute("
            CREATE INDEX IF NOT EXISTS idx_sessions_token
            ON sessions(token)
        ");
    }
}
