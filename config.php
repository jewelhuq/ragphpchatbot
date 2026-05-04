<?php

// ============================================================
// RAG System Configuration
// ============================================================
// This is the single place where all settings live.
// Change a value here and it affects the entire system.
// ============================================================

return [

    // ----------------------------------------------------------
    // TURSO DATABASE
    // Where we store document chunks and their embeddings.
    // Turso is a cloud SQLite database with native vector search.
    // ----------------------------------------------------------
    'turso_url'   => 'https://engi-rag-test-engi.aws-us-east-1.turso.io',
    'turso_token' => 'eyJhbGciOiJFZERTQSIsInR5cCI6IkpXVCJ9.eyJhIjoicnciLCJpYXQiOjE3Nzc5MTAxMTQsImlkIjoiMDE5ZGYzYjMtNGMwMS03MDk3LWI0OTgtYWZmNDQwMjM3NjE5IiwicmlkIjoiZjhjZGNhNzYtYjIxYy00OWQ4LTg5YTYtYzkyNGZlMjhlYjJhIn0.zAY421qhFirLo0PI3mMqBEfsCKArbddbPfLYa0yEfdmVJr7k5Pwe9NdqkmRl6rIM691yMRa3Haq-dv3b7j4nAQ',

    // ----------------------------------------------------------
    // OLLAMA (Local AI)
    // Ollama runs AI models on your own machine.
    // We use two models:
    //   - chat_model:      generates the final answer
    //   - embedding_model: converts text into numbers (vectors)
    //                      so we can measure similarity
    // ----------------------------------------------------------
    'ollama_url'      => 'http://127.0.0.1:11434',
    'chat_model'      => 'gemma3:1b',
    'embedding_model' => 'nomic-embed-text',
    'embedding_dims'  => 768, // nomic=768 dims | mxbai-embed-large=1024 dims

    // ----------------------------------------------------------
    // DOCUMENT INGESTION
    // Controls how we read and split documents before storing them.
    //
    // chunk_size:    max characters per chunk (~1500 = ~250 words)
    // chunk_overlap: how many chars carry over to the next chunk
    //                so sentences on boundaries don't lose context
    // docs_dir:      folder where you drop your documents
    // ----------------------------------------------------------
    'chunk_size'    => 1500,
    'chunk_overlap' => 150,
    'docs_dir'      => __DIR__ . '/docs',

    // ----------------------------------------------------------
    // RETRIEVAL
    // top_k: how many chunks to pull from the database per query.
    // Higher = more context for the AI but slower and more tokens.
    // 5 is a good balance for most use cases.
    // ----------------------------------------------------------
    'top_k' => 5,

    // ----------------------------------------------------------
    // CENTRAL API KEYS (workspaces can override per-workspace)
    // ----------------------------------------------------------
    'openai_key'    => '',    'voyage_key'    => '',

    // ----------------------------------------------------------
    // SECURITY
    // encryption_key: used to encrypt API keys stored in DB
    // Change this to a random 32-char string in production
    // ----------------------------------------------------------
    'encryption_key' => 'change-this-to-a-random-32-char-key!!',

    // ----------------------------------------------------------
    // STORAGE
    // ----------------------------------------------------------
    'storage_dir' => __DIR__ . '/storage/workspaces',

];
