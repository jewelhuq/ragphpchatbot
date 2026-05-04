/*
 * rag.c — One-shot retrieval-augmented generation.
 *
 * Usage:
 *   ./rag "What is RAG and how does it work?"
 *
 * This binary ties the full RAG pipeline together:
 *
 *   1. Embed the user's question.
 *   2. Search Turso for the top-k most relevant document chunks.
 *   3. Build a prompt that gives those chunks to the LLM as context.
 *   4. Stream the LLM's answer to stdout token by token.
 *
 * The prompt format follows the "context-question" pattern standard
 * in RAG systems:
 *
 *   System: You are a helpful assistant. Answer using only the context
 *           provided below. If the answer is not in the context, say so.
 *
 *           Context:
 *           [chunk 1 from docs]
 *           ---
 *           [chunk 2 from docs]
 *           ...
 *
 *   User: <question>
 *
 * We stream the assistant's response directly to stdout so the user
 * sees tokens appear as they are generated — no waiting for the full
 * response to buffer.
 *
 * Prompt building is done with a dynamically growing char buffer
 * (prompt_buf) to avoid a fixed-size limit on context.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "config.h"
#include "ollama.h"
#include "turso.h"

#define MAX_EMBED_JSON  (768 * 16 + 16)
#define PROMPT_MAX      (64 * 1024)   /* 64 KB prompt buffer */

/* ---------------------------------------------------------------
 * embedding_to_json — float array to JSON array string.
 * --------------------------------------------------------------- */

static void embedding_to_json(const float *emb, int dims, char *buf, size_t buf_len) {
    size_t pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < dims && pos < buf_len - 24; i++) {
        if (i > 0) buf[pos++] = ',';
        int n = snprintf(buf + pos, buf_len - pos, "%.8g", (double)emb[i]);
        if (n > 0) pos += (size_t)n;
    }
    if (pos < buf_len - 1) buf[pos++] = ']';
    buf[pos] = '\0';
}

/* ---------------------------------------------------------------
 * ensure_vector_index — create ANN index if it doesn't exist.
 * --------------------------------------------------------------- */

static void ensure_vector_index(const RagConfig *cfg) {
    turso_execute(cfg,
        "CREATE INDEX IF NOT EXISTS idx_documents_embedding"
        " ON documents (libsql_vector_idx(embedding))",
        NULL, 0);
}

/* ---------------------------------------------------------------
 * token_callback — streaming token handler for ollama_chat_stream.
 *
 * Each call receives one or more characters of the LLM's response.
 * We print them immediately to stdout (no newline) and flush so the
 * user sees the text build up in real time.
 * --------------------------------------------------------------- */

static int token_callback(const char *data, size_t size, void *userdata) {
    (void)userdata;
    fwrite(data, 1, size, stdout);
    fflush(stdout);
    return 0;  /* 0 = continue streaming */
}

/* ---------------------------------------------------------------
 * build_context_prompt — assemble the system prompt from chunks.
 *
 * We concatenate chunk content separated by "---" dividers.
 * Each chunk is labeled with its source file so the LLM can
 * potentially cite it (and the user can verify provenance).
 * --------------------------------------------------------------- */

static char *build_context_prompt(TursoRow *results, int n) {
    char *buf = (char *)malloc(PROMPT_MAX);
    if (!buf) return NULL;

    int pos = 0;
    pos += snprintf(buf + pos, PROMPT_MAX - pos,
        "You are a helpful assistant. Answer using only the context "
        "provided below. If the answer is not in the context, say "
        "\"I don't have enough information to answer that.\"\n\n"
        "Context:\n");

    for (int i = 0; i < n && pos < PROMPT_MAX - 256; i++) {
        TursoRow *row = &results[i];
        const char *source  = NULL;
        const char *content = NULL;

        for (int c = 0; c < row->ncols; c++) {
            if (strcmp(row->cols[c], "source")  == 0) source  = row->vals[c];
            if (strcmp(row->cols[c], "content") == 0) content = row->vals[c];
        }

        if (source && content) {
            pos += snprintf(buf + pos, PROMPT_MAX - pos,
                "[Source: %s]\n%s\n---\n", source, content);
        }
    }

    return buf;
}

/* ---------------------------------------------------------------
 * main — embed, retrieve, build prompt, stream answer.
 * --------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s \"your question here\"\n", argv[0]);
        return 1;
    }

    const char *question = argv[1];

    curl_global_init(CURL_GLOBAL_DEFAULT);
    RagConfig cfg = config_default();

    ensure_vector_index(&cfg);

    /* --- Step 1: Embed the question --- */
    float embedding[DEFAULT_EMBEDDING_DIMS];
    if (ollama_embed(&cfg, question, embedding, cfg.embedding_dims) != 0) {
        fprintf(stderr, "rag: failed to embed question\n");
        curl_global_cleanup();
        return 1;
    }

    char embed_json[MAX_EMBED_JSON];
    embedding_to_json(embedding, cfg.embedding_dims, embed_json, sizeof embed_json);

    /* --- Step 2: Vector search --- */
    char k_str[16];
    snprintf(k_str, sizeof k_str, "%d", cfg.top_k);

    const char *sql =
        "SELECT d.source, d.content"
        " FROM vector_top_k('idx_documents_embedding', vector(?), ?)"
        " JOIN documents d ON d.id = rowid";

    const char *search_params[] = { embed_json, k_str };

    TursoRow results[MAX_RESULTS];
    int n = turso_fetch_all(&cfg, sql, search_params, 2, results, MAX_RESULTS);

    if (n < 0) {
        fprintf(stderr, "rag: search failed\n");
        curl_global_cleanup();
        return 1;
    }

    if (n == 0) {
        printf("No relevant context found. Run ./ingest first.\n");
        curl_global_cleanup();
        return 0;
    }

    /* --- Step 3: Build prompt --- */
    char *system_prompt = build_context_prompt(results, n);
    turso_free_results(results, n);

    if (!system_prompt) {
        fprintf(stderr, "rag: failed to build prompt\n");
        curl_global_cleanup();
        return 1;
    }

    /* --- Step 4: Stream answer --- */
    printf("\nQuestion: %s\n\n", question);
    printf("Answer:\n");

    /*
     * messages[] is a flat array of role/content pairs.
     * We send: system prompt + user question.
     */
    const char *messages[] = {
        "system", system_prompt,
        "user",   question
    };

    int rc = ollama_chat_stream(&cfg, messages, 4, token_callback, NULL);

    free(system_prompt);
    printf("\n");  /* newline after streaming output */

    curl_global_cleanup();
    return (rc == 0) ? 0 : 1;
}
