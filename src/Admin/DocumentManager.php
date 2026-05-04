<?php

namespace RAG\Admin;

use RAG\Turso;
use RAG\Ollama;
use RAG\DocumentParser;
use RAG\Chunker;

// ============================================================
// DocumentManager — Upload, Ingest, and Manage Documents
// ============================================================
// Each document belongs to one workspace.
// Files are saved to storage/workspaces/{workspace_id}/
// Chunks are stored in the chunks table with workspace_id.
//
// Ingestion is streamed via SSE so the admin sees real-time
// progress: "Embedding chunk 3/12..."
// ============================================================

class DocumentManager
{
    private Turso  $turso;
    private string $storageBase;

    const MAX_FILE_SIZE  = 104857600; // 100MB
    const ALLOWED_TYPES  = ['txt', 'pdf', 'docx'];

    public function __construct(Turso $turso, string $storageBase)
    {
        $this->turso       = $turso;
        $this->storageBase = rtrim($storageBase, '/');
    }

    // ----------------------------------------------------------
    // Handle a file upload for a workspace.
    // Validates, saves to disk, creates a document record.
    // Returns the document row (status = 'pending').
    // ----------------------------------------------------------
    public function upload(int $workspaceId, array $uploadedFile): array
    {
        $originalName = $uploadedFile['name'];
        $tmpPath      = $uploadedFile['tmp_name'];
        $fileSize     = $uploadedFile['size'];
        $extension    = strtolower(pathinfo($originalName, PATHINFO_EXTENSION));

        if (!in_array($extension, self::ALLOWED_TYPES)) {
            throw new \InvalidArgumentException("Unsupported file type: .$extension. Allowed: txt, pdf, docx");
        }

        if ($fileSize > self::MAX_FILE_SIZE) {
            throw new \InvalidArgumentException("File too large. Maximum size is 100MB.");
        }

        // Ensure workspace storage directory exists
        $storageDir = $this->storageBase . '/' . $workspaceId;
        if (!is_dir($storageDir)) {
            mkdir($storageDir, 0755, true);
        }

        // Save with a unique filename to avoid collisions
        $safeFilename = uniqid('doc_') . '.' . $extension;
        $savedPath    = $storageDir . '/' . $safeFilename;

        if (!move_uploaded_file($tmpPath, $savedPath)) {
            throw new \RuntimeException("Failed to save uploaded file.");
        }

        $fileHash = md5_file($savedPath);

        // Check for duplicate in this workspace
        $duplicate = $this->turso->fetchAll(
            "SELECT id FROM documents WHERE workspace_id = ? AND file_hash = ?",
            [$workspaceId, $fileHash]
        );

        if (!empty($duplicate)) {
            unlink($savedPath);
            throw new \RuntimeException("This file has already been ingested in this workspace.");
        }

        $this->turso->execute(
            "INSERT INTO documents (workspace_id, filename, original_name, file_hash, file_size, status)
             VALUES (?, ?, ?, ?, ?, 'pending')",
            [$workspaceId, $safeFilename, $originalName, $fileHash, $fileSize]
        );

        return $this->turso->fetchAll(
            "SELECT * FROM documents WHERE workspace_id = ? ORDER BY id DESC LIMIT 1",
            [$workspaceId]
        )[0];
    }

    // ----------------------------------------------------------
    // Ingest a document — stream progress via SSE.
    // Call this after upload() in a separate SSE endpoint.
    // $onProgress(string $message) is called for each chunk.
    // ----------------------------------------------------------
    public function ingest(
        int      $documentId,
        int      $workspaceId,
        array    $workspaceSettings,
        callable $onProgress
    ): void {
        $doc = $this->turso->fetchAll(
            "SELECT * FROM documents WHERE id = ? AND workspace_id = ?",
            [$documentId, $workspaceId]
        );

        if (empty($doc)) throw new \RuntimeException("Document not found.");
        $doc = $doc[0];

        $filePath = $this->storageBase . '/' . $workspaceId . '/' . $doc['filename'];
        if (!file_exists($filePath)) throw new \RuntimeException("File not found on disk.");

        // Mark as ingesting
        $this->turso->execute(
            "UPDATE documents SET status = 'ingesting' WHERE id = ?", [$documentId]
        );

        try {
            $parser  = new DocumentParser();
            $chunker = new Chunker(
                $workspaceSettings['chunk_size']    ?? 1500,
                $workspaceSettings['chunk_overlap'] ?? 150
            );
            $ollama  = new Ollama($workspaceSettings);

            $onProgress("Parsing document...");
            $text   = $parser->parse($filePath);
            $chunks = $chunker->chunk($text, pathinfo($doc['original_name'], PATHINFO_FILENAME));
            $total  = count($chunks);

            $onProgress("Split into $total chunks. Starting embedding...");

            // Ensure chunks table exists with correct dims
            $dims = $workspaceSettings['embedding_dims'] ?? 768;
            $this->turso->execute("
                CREATE TABLE IF NOT EXISTS chunks (
                    id           INTEGER PRIMARY KEY AUTOINCREMENT,
                    workspace_id INTEGER NOT NULL,
                    source       TEXT    NOT NULL,
                    chunk_idx    INTEGER NOT NULL,
                    content      TEXT    NOT NULL,
                    embedding    F32_BLOB($dims) NOT NULL,
                    file_hash    TEXT    NOT NULL DEFAULT ''
                )
            ");
            $this->turso->execute("
                CREATE INDEX IF NOT EXISTS chunks_vector_idx
                ON chunks(libsql_vector_idx(embedding))
            ");
            $this->turso->execute("
                CREATE INDEX IF NOT EXISTS idx_chunks_workspace
                ON chunks(workspace_id)
            ");

            // Remove old chunks for this document if re-ingesting
            $this->turso->execute(
                "DELETE FROM chunks WHERE workspace_id = ? AND source = ?",
                [$workspaceId, $doc['original_name']]
            );

            foreach ($chunks as $index => $chunkText) {
                $onProgress("Embedding chunk " . ($index + 1) . " of $total...");

                $embedding = $ollama->embed($chunkText);
                $vecStr    = '[' . implode(',', $embedding) . ']';

                $this->turso->execute(
                    "INSERT INTO chunks (workspace_id, source, chunk_idx, content, embedding, file_hash)
                     VALUES (?, ?, ?, ?, vector32(?), ?)",
                    [$workspaceId, $doc['original_name'], $index, $chunkText, $vecStr, $doc['file_hash']]
                );
            }

            $this->turso->execute(
                "UPDATE documents SET status = 'done', chunk_count = ? WHERE id = ?",
                [$total, $documentId]
            );

            $onProgress("Done! $total chunks ingested.");

        } catch (\Exception $e) {
            $this->turso->execute(
                "UPDATE documents SET status = 'error', error = ? WHERE id = ?",
                [$e->getMessage(), $documentId]
            );
            throw $e;
        }
    }

    // ----------------------------------------------------------
    // List all documents for a workspace.
    // ----------------------------------------------------------
    public function list(int $workspaceId): array
    {
        return $this->turso->fetchAll(
            "SELECT * FROM documents WHERE workspace_id = ? ORDER BY created_at DESC",
            [$workspaceId]
        );
    }

    // ----------------------------------------------------------
    // Delete a document and all its chunks.
    // ----------------------------------------------------------
    public function delete(int $documentId, int $workspaceId): void
    {
        $doc = $this->turso->fetchAll(
            "SELECT * FROM documents WHERE id = ? AND workspace_id = ?",
            [$documentId, $workspaceId]
        );

        if (empty($doc)) throw new \RuntimeException("Document not found.");
        $doc = $doc[0];

        // Delete chunks from vector store
        $this->turso->execute(
            "DELETE FROM chunks WHERE workspace_id = ? AND source = ?",
            [$workspaceId, $doc['original_name']]
        );

        // Delete the file from disk
        $filePath = $this->storageBase . '/' . $workspaceId . '/' . $doc['filename'];
        if (file_exists($filePath)) unlink($filePath);

        // Delete the document record
        $this->turso->execute(
            "DELETE FROM documents WHERE id = ?", [$documentId]
        );
    }
}
