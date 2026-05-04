/*
 * query.c — Vector similarity search CLI tool.
 *
 * Usage:
 *   ./query "your question here"
 *
 * This binary:
 *   1. Takes the user's query from argv[1].
 *   2. Embeds it with Ollama (nomic-embed-text).
 *   3. Runs a vector_top_k() search against Turso.
 *   4. Prints the top-k matching chunks with their sources.
 *
 * It does NOT generate an AI answer — that's rag.c's job.  query.c is
 * useful for inspecting what the retrieval step produces before the
 * LLM gets involved, which helps debug relevance issues.
 *
 * Turso vector search SQL:
 *
 *   SELECT d.source, d.content, d.chunk_idx,
 *          vector_distance_cos(d.embedding, vector(?)) AS distance
 *   FROM vector_top_k('idx_documents_embedding', vector(?), ?)
 *   JOIN documents d ON d.id = rowid
 *   ORDER BY distance;
 *
 * The vector_top_k() table-valued function takes:
 *   - index name (the ANN index we create on the embedding column)
 *   - query vector (as JSON float array passed through vector())
 *   - k (number of results)
 *
 * Note: Turso requires a separate CREATE VECTOR INDEX statement for
 * ANN search.  We create it here if it doesn't exist.  The index name
 * must match what we use in vector_top_k().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "config.h"
#include "ollama.h"
#include "turso.h"

#define MAX_EMBED_JSON (768 * 16 + 16)

/* ---------------------------------------------------------------
 * embedding_to_json — float array to "[f0,f1,...]" string.
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
 * ensure_vector_index — create the Turso ANN index if missing.
 *
 * Turso uses a separate DDL statement to build the approximate
 * nearest-neighbour index.  Without it, vector_top_k() fails.
 * We use IF NOT EXISTS so re-running is safe.
 * --------------------------------------------------------------- */

static void ensure_vector_index(const RagConfig *cfg) {
    const char *sql =
        "CREATE INDEX IF NOT EXISTS idx_documents_embedding"
        " ON documents (libsql_vector_idx(embedding))";
    /* Ignore errors — index may already exist or table may be empty */
    turso_execute(cfg, sql, NULL, 0);
}

/* ---------------------------------------------------------------
 * print_separator — visual divider between results.
 * --------------------------------------------------------------- */

static void print_separator(void) {
    printf("-----------------------------------------------------------\n");
}

/* ---------------------------------------------------------------
 * main — embed the query and print top-k results.
 * --------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s \"your query here\"\n", argv[0]);
        return 1;
    }

    const char *query = argv[1];

    curl_global_init(CURL_GLOBAL_DEFAULT);
    RagConfig cfg = config_default();

    /* Ensure the vector index exists */
    ensure_vector_index(&cfg);

    /* Embed the query */
    printf("Embedding query: \"%s\"\n\n", query);
    float embedding[DEFAULT_EMBEDDING_DIMS];
    if (ollama_embed(&cfg, query, embedding, cfg.embedding_dims) != 0) {
        fprintf(stderr, "Failed to embed query\n");
        curl_global_cleanup();
        return 1;
    }

    /* Convert to JSON for the SQL parameter */
    char embed_json[MAX_EMBED_JSON];
    embedding_to_json(embedding, cfg.embedding_dims, embed_json, sizeof embed_json);

    /* Build k as string parameter */
    char k_str[16];
    snprintf(k_str, sizeof k_str, "%d", cfg.top_k);

    /*
     * Vector search SQL.
     *
     * vector_top_k(index_name, query_vector, k) is a table-valued
     * function that returns (id, distance) for the k nearest vectors.
     * We JOIN back to documents to get content and source.
     *
     * We pass embed_json twice: once for vector_top_k and once for
     * vector_distance_cos to get the actual distance value.
     */
    const char *sql =
        "SELECT d.source, d.content, d.chunk_idx,"
        "       vector_distance_cos(d.embedding, vector(?)) AS distance"
        " FROM vector_top_k('idx_documents_embedding', vector(?), ?)"
        " JOIN documents d ON d.id = rowid"
        " ORDER BY distance ASC";

    const char *params[] = { embed_json, embed_json, k_str };

    TursoRow results[MAX_RESULTS];
    int n = turso_fetch_all(&cfg, sql, params, 3, results, MAX_RESULTS);

    if (n < 0) {
        fprintf(stderr, "Vector search failed\n");
        curl_global_cleanup();
        return 1;
    }

    if (n == 0) {
        printf("No results found. Run ./ingest first to populate the database.\n");
        curl_global_cleanup();
        return 0;
    }

    printf("Top %d results for: \"%s\"\n\n", n, query);

    for (int i = 0; i < n; i++) {
        TursoRow *row = &results[i];

        /* Find column values by name */
        const char *source   = NULL;
        const char *content  = NULL;
        const char *chunk_idx = NULL;
        const char *distance = NULL;

        for (int c = 0; c < row->ncols; c++) {
            if (strcmp(row->cols[c], "source")    == 0) source    = row->vals[c];
            if (strcmp(row->cols[c], "content")   == 0) content   = row->vals[c];
            if (strcmp(row->cols[c], "chunk_idx") == 0) chunk_idx = row->vals[c];
            if (strcmp(row->cols[c], "distance")  == 0) distance  = row->vals[c];
        }

        print_separator();
        printf("Result %d | Source: %s | Chunk: %s | Distance: %s\n",
               i + 1,
               source   ? source   : "?",
               chunk_idx ? chunk_idx : "?",
               distance  ? distance  : "?");
        print_separator();
        if (content) {
            /* Print up to 400 chars of the chunk for readability */
            int content_len = (int)strlen(content);
            int print_len   = content_len > 400 ? 400 : content_len;
            printf("%.*s", print_len, content);
            if (content_len > 400) printf("\n... [truncated]");
            printf("\n");
        }
        printf("\n");
    }

    turso_free_results(results, n);
    curl_global_cleanup();
    return 0;
}
