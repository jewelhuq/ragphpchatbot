/*
 * ingest.c — Document ingestion pipeline.
 *
 * This binary reads all .txt and .md files from the docs/ directory,
 * splits them into chunks, embeds each chunk with Ollama, and stores
 * the results in Turso.  It is designed to be run once (or re-run to
 * refresh documents that changed).
 *
 * The pipeline for each file:
 *
 *   1. Read file into memory.
 *   2. Compute MD5 hash of the content (using OpenSSL).
 *   3. Check Turso: does this file already exist with this hash?
 *      - Yes, same hash → skip (no-op, file unchanged).
 *      - Yes, different hash → delete old chunks, re-ingest.
 *      - No → ingest fresh.
 *   4. Split into chunks (chunker.c).
 *   5. For each chunk: embed with Ollama → insert into Turso.
 *
 * Schema:
 *   CREATE TABLE IF NOT EXISTS documents (
 *     id        INTEGER PRIMARY KEY AUTOINCREMENT,
 *     source    TEXT NOT NULL,
 *     hash      TEXT NOT NULL,
 *     chunk_idx INTEGER NOT NULL,
 *     content   TEXT NOT NULL,
 *     embedding F32_BLOB(768)
 *   );
 *   CREATE INDEX IF NOT EXISTS idx_documents_source ON documents(source);
 *
 * Vector storage: Turso stores embeddings as F32_BLOB(768).  We pass
 * the float array as a raw binary blob using the vector32() helper
 * function in Turso's SQL dialect.  We convert the float array to a
 * hex string and pass it as a parameter — Turso converts it internally.
 *
 * Actually, the cleanest approach for Turso HTTP is to pass the
 * embedding as a JSON array of floats directly in the SQL, which Turso
 * handles with the vector('[...]') function.  That avoids binary
 * encoding complexities over HTTP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/*
 * OpenSSL 3.x deprecates the direct MD5() function in favour of the EVP
 * interface.  We suppress the deprecation warning here because MD5 is only
 * used for file change-detection (not security), and the direct API is still
 * present in OpenSSL 3.x — just discouraged for new cryptographic use.
 */
#define OPENSSL_API_COMPAT 0x10100000L
#include <openssl/md5.h>
#include <curl/curl.h>

#include "config.h"
#include "turso.h"
#include "ollama.h"
#include "chunker.h"

/* docs/ directory relative to where the binary is run */
#define DOCS_DIR "../../docs"

/* Maximum file size we'll read into memory (8 MB) */
#define MAX_FILE_SIZE (8 * 1024 * 1024)

/* Maximum embedding JSON string length: 768 floats * ~15 chars each + brackets */
#define MAX_EMBED_JSON (768 * 16 + 16)

/* ---------------------------------------------------------------
 * read_file — load a file into a heap-allocated buffer.
 *
 * Returns a null-terminated malloc'd string containing the file
 * content, or NULL on failure.  Sets *out_len to the byte count.
 * --------------------------------------------------------------- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ingest: cannot open %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > MAX_FILE_SIZE) {
        fprintf(stderr, "ingest: skipping %s (size %ld)\n", path, size);
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t read_count = fread(buf, 1, (size_t)size, f);
    fclose(f);

    buf[read_count] = '\0';
    if (out_len) *out_len = read_count;
    return buf;
}

/* ---------------------------------------------------------------
 * md5_hex — compute MD5 of data and format as 32-char hex string.
 *
 * out_hex must be at least 33 bytes.
 * --------------------------------------------------------------- */

static void md5_hex(const char *data, size_t len, char *out_hex) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((const unsigned char *)data, len, digest);
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        snprintf(out_hex + i * 2, 3, "%02x", digest[i]);
    }
    out_hex[32] = '\0';
}

/* ---------------------------------------------------------------
 * embedding_to_json — convert float array to JSON array string.
 *
 * Produces "[0.12,-0.34,...]" in buf.  Used to pass the embedding
 * to Turso's vector() function.
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
 * ensure_schema — create the documents table and index if missing.
 *
 * We use CREATE TABLE IF NOT EXISTS so this is safe to call on every
 * run.  The F32_BLOB(768) column type is Turso's extension for storing
 * IEEE 754 float vectors in compact binary form.
 * --------------------------------------------------------------- */

static int ensure_schema(const RagConfig *cfg) {
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS documents ("
        "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source    TEXT NOT NULL,"
        "  hash      TEXT NOT NULL,"
        "  chunk_idx INTEGER NOT NULL,"
        "  content   TEXT NOT NULL,"
        "  embedding F32_BLOB(768)"
        ")";

    if (turso_execute(cfg, create_sql, NULL, 0) != 0) {
        fprintf(stderr, "ingest: failed to create documents table\n");
        return -1;
    }

    const char *index_sql =
        "CREATE INDEX IF NOT EXISTS idx_documents_source ON documents(source)";

    if (turso_execute(cfg, index_sql, NULL, 0) != 0) {
        fprintf(stderr, "ingest: failed to create index\n");
        return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------
 * get_stored_hash — query Turso for the hash of a previously-ingested file.
 *
 * Returns 1 and fills stored_hash[] if found, 0 if not found, -1 on error.
 * --------------------------------------------------------------- */

static int get_stored_hash(const RagConfig *cfg, const char *source, char *stored_hash) {
    const char *params[] = { source };
    TursoRow    rows[2];
    int         n = turso_fetch_all(cfg,
                                    "SELECT hash FROM documents WHERE source=? LIMIT 1",
                                    params, 1, rows, 2);
    if (n < 0) return -1;
    if (n == 0) return 0;

    const char *h = rows[0].vals[0];
    if (h) strncpy(stored_hash, h, 33);
    turso_free_results(rows, n);
    return 1;
}

/* ---------------------------------------------------------------
 * delete_chunks — remove all rows for a given source file.
 * --------------------------------------------------------------- */

static int delete_chunks(const RagConfig *cfg, const char *source) {
    const char *params[] = { source };
    return turso_execute(cfg, "DELETE FROM documents WHERE source=?", params, 1);
}

/* ---------------------------------------------------------------
 * ingest_file — full pipeline for one document.
 * --------------------------------------------------------------- */

static int ingest_file(const RagConfig *cfg, const char *path, const char *filename) {
    printf("  Ingesting: %s\n", filename);

    /* 1. Read file */
    size_t file_len;
    char *content = read_file(path, &file_len);
    if (!content) return -1;

    /* 2. Hash */
    char hash[33];
    md5_hex(content, file_len, hash);

    /* 3. Check existing */
    char stored_hash[33] = {0};
    int found = get_stored_hash(cfg, filename, stored_hash);
    if (found < 0) { free(content); return -1; }
    if (found == 1 && strcmp(hash, stored_hash) == 0) {
        printf("    Unchanged (hash match), skipping.\n");
        free(content);
        return 0;
    }
    if (found == 1) {
        printf("    Changed, re-ingesting...\n");
        if (delete_chunks(cfg, filename) != 0) {
            free(content);
            return -1;
        }
    }

    /* 4. Chunk */
    Chunk chunks[MAX_CHUNKS];
    int n_chunks = chunk_text(content, filename, chunks, MAX_CHUNKS, cfg->chunk_size);
    free(content);

    if (n_chunks < 0) {
        fprintf(stderr, "    Chunking failed\n");
        return -1;
    }
    printf("    %d chunks produced\n", n_chunks);

    /* 5. Embed and insert each chunk */
    float embedding[DEFAULT_EMBEDDING_DIMS];
    char  embed_json[MAX_EMBED_JSON];
    int   success = 0;

    for (int i = 0; i < n_chunks; i++) {
        printf("    Chunk %d/%d...", i + 1, n_chunks);
        fflush(stdout);

        /* Embed */
        if (ollama_embed(cfg, chunks[i].text, embedding, cfg->embedding_dims) != 0) {
            fprintf(stderr, " embed failed\n");
            continue;
        }

        /* Convert embedding to JSON array string */
        embedding_to_json(embedding, cfg->embedding_dims, embed_json, sizeof embed_json);

        /*
         * Build the INSERT SQL using Turso's vector() function.
         * We pass the embedding as a string param and let Turso parse it.
         * The SQL uses vector(?) to convert the JSON float array to F32_BLOB.
         */
        char chunk_idx_str[16];
        snprintf(chunk_idx_str, sizeof chunk_idx_str, "%d", i);

        const char *params[] = {
            filename,       /* source    */
            hash,           /* hash      */
            chunk_idx_str,  /* chunk_idx */
            chunks[i].text, /* content   */
            embed_json      /* embedding JSON, converted by vector() */
        };

        const char *insert_sql =
            "INSERT INTO documents (source, hash, chunk_idx, content, embedding)"
            " VALUES (?, ?, ?, ?, vector(?))";

        if (turso_execute(cfg, insert_sql, params, 5) != 0) {
            fprintf(stderr, " insert failed\n");
            continue;
        }

        printf(" done\n");
        success++;
    }

    chunker_free_chunks(chunks, n_chunks);
    printf("    Inserted %d/%d chunks\n", success, n_chunks);
    return (success > 0) ? 0 : -1;
}

/* ---------------------------------------------------------------
 * main — scan docs/ directory and ingest supported file types.
 * --------------------------------------------------------------- */

int main(void) {
    printf("=== C RAG Ingest ===\n");

    /* Initialize libcurl globally (required before any http_* call) */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    RagConfig cfg = config_default();

    /* Ensure schema exists */
    printf("Ensuring schema...\n");
    if (ensure_schema(&cfg) != 0) {
        fprintf(stderr, "Schema setup failed. Exiting.\n");
        curl_global_cleanup();
        return 1;
    }
    printf("Schema OK.\n\n");

    /* Open docs directory */
    DIR *dir = opendir(DOCS_DIR);
    if (!dir) {
        fprintf(stderr, "Cannot open docs directory: %s\n", DOCS_DIR);
        fprintf(stderr, "Run ingest from C:\\Users\\Engi\\Ai\\RAG\\implementations\\c\\\n");
        curl_global_cleanup();
        return 1;
    }

    int file_count = 0;
    int fail_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;

        /* Skip hidden files and directories */
        if (name[0] == '.') continue;

        /* Only process .txt and .md files */
        size_t nlen = strlen(name);
        int is_txt = (nlen > 4 && strcmp(name + nlen - 4, ".txt") == 0);
        int is_md  = (nlen > 3 && strcmp(name + nlen - 3, ".md")  == 0);
        if (!is_txt && !is_md) continue;

        /* Build full path */
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", DOCS_DIR, name);

        /* Check it's a regular file */
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        file_count++;
        if (ingest_file(&cfg, path, name) != 0) {
            fail_count++;
        }
        printf("\n");
    }
    closedir(dir);

    printf("=== Done: %d files processed, %d failures ===\n",
           file_count, fail_count);

    curl_global_cleanup();
    return (fail_count > 0) ? 1 : 0;
}
