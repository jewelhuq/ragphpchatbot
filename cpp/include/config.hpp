/**
 * @file config.hpp
 * @brief Central configuration for the RAG system.
 *
 * Every journey needs a map. This file is that map. RagConfig holds every
 * URL, token, model name, and tuning knob the system needs so that no
 * magic string is ever buried inside business logic. Call
 * RagConfig::defaults() to get a fully populated configuration ready for
 * immediate use against the real Turso and Ollama endpoints.
 */

#pragma once

#include <string>

/**
 * @brief Aggregates every runtime parameter the RAG pipeline depends on.
 *
 * All fields are plain std::string so they can be trivially overridden from
 * environment variables or a future config-file loader without touching
 * call-sites. Numeric values (chunk size, overlap, dimensions) are stored as
 * strings for the same reason and converted at the point of use.
 */
struct RagConfig {
    // -----------------------------------------------------------------------
    // Turso / libSQL
    // -----------------------------------------------------------------------

    /** Base URL of the Turso HTTP API, e.g. https://<db>.turso.io */
    std::string tursoUrl;

    /** Bearer token that authenticates every request to Turso. */
    std::string tursoToken;

    /** Name of the table that stores document chunks and their embeddings. */
    std::string tableName;

    // -----------------------------------------------------------------------
    // Ollama
    // -----------------------------------------------------------------------

    /** Base URL of the locally-running Ollama daemon. */
    std::string ollamaUrl;

    /** Model used to turn text into embedding vectors. */
    std::string embedModel;

    /** Model used for chat / answer generation. */
    std::string chatModel;

    /** Number of dimensions produced by the embedding model. */
    int embeddingDims;

    // -----------------------------------------------------------------------
    // Chunking
    // -----------------------------------------------------------------------

    /** Desired character length of each chunk (soft limit). */
    int chunkSize;

    /** Character overlap between consecutive chunks for context continuity. */
    int chunkOverlap;

    // -----------------------------------------------------------------------
    // Retrieval
    // -----------------------------------------------------------------------

    /** How many nearest-neighbour chunks to retrieve per query. */
    int topK;

    // -----------------------------------------------------------------------
    // Ingest
    // -----------------------------------------------------------------------

    /** Directory the ingest tool scans for source documents. */
    std::string docsDir;

    // -----------------------------------------------------------------------
    // Factory
    // -----------------------------------------------------------------------

    /**
     * @brief Returns a RagConfig pre-populated with all real endpoint values.
     *
     * This is the single source of truth for production settings. Override
     * individual fields after calling defaults() if you need to point at a
     * different environment.
     *
     * @return A fully initialised RagConfig ready for immediate use.
     */
    static RagConfig defaults() {
        RagConfig cfg;

        // Turso
        cfg.tursoUrl   = "https://your-db-name.turso.io";
        cfg.tursoToken = "your-turso-token-here"
                         ".eyJhIjoicnciLCJpYXQiOjE3NDYzMDM4MDksImlkIjoiZmZkZjQwY2UtNGQ4MS00ZjIwLWJmMWMtNmMzNzE0ZGFkODBjIn0"
                         ".iFkAJSZhIbZ8nKx6V6nxaEFhBFABPlbWq1DPPFM7bFrXX7bPTRy_Y63Gg7qsNGt2Gt8M7y_5CGfPynnV4geCg";
        cfg.tableName  = "documents";

        // Ollama
        cfg.ollamaUrl     = "http://127.0.0.1:11434";
        cfg.embedModel    = "nomic-embed-text";
        cfg.chatModel     = "gemma3:1b";
        cfg.embeddingDims = 768;

        // Chunking
        cfg.chunkSize    = 512;
        cfg.chunkOverlap = 64;

        // Retrieval
        cfg.topK = 5;

        // Ingest
        cfg.docsDir = "docs";

        return cfg;
    }
};
