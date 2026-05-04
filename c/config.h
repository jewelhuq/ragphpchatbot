/*
 * config.h — Central configuration for the C RAG system.
 *
 * Every meaningful constant and runtime setting lives here.
 * The philosophy is "one place to change everything" — if you
 * swap models, change the Turso endpoint, or tune chunk_size,
 * you edit this file and recompile. No hunting through source.
 *
 * RagConfig is a plain struct of char pointers and ints.
 * Keeping it simple means we avoid dynamic allocation for
 * configuration — config_default() fills a stack-allocated
 * struct and callers pass it by pointer everywhere.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ---------------------------------------------------------------
 * Hard-wired defaults — change these before compiling if you
 * want different values baked into the binary.
 * --------------------------------------------------------------- */
#define DEFAULT_TURSO_URL       "https://your-db-name.turso.io"
#define DEFAULT_TURSO_TOKEN     "your-turso-token-here"
#define DEFAULT_OLLAMA_URL      "http://127.0.0.1:11434"
#define DEFAULT_CHAT_MODEL      "gemma3:1b"
#define DEFAULT_EMBEDDING_MODEL "nomic-embed-text"
#define DEFAULT_EMBEDDING_DIMS  768
#define DEFAULT_TOP_K           5
#define DEFAULT_CHUNK_SIZE      1500

/* Maximum sizes for internal buffers. */
#define MAX_RESPONSE_BUF        (4 * 1024 * 1024)  /* 4 MB HTTP response buffer  */
#define MAX_HEADERS             16                  /* max HTTP headers per call  */
#define MAX_CHUNKS              512                 /* max chunks per document    */
#define MAX_RESULTS             64                  /* max turso result rows      */
#define MAX_COLS                16                  /* max columns per row        */
#define MAX_MESSAGES            128                 /* max chat history messages  */

/* ---------------------------------------------------------------
 * RagConfig — runtime configuration struct.
 *
 * All char* fields point into string literals (DEFAULT_* macros)
 * or heap memory if overridden at runtime. The caller owns the
 * lifetime; config_default() fills with static literals so no
 * free() is needed for the default case.
 * --------------------------------------------------------------- */
typedef struct {
    const char *turso_url;        /* Turso HTTP endpoint                  */
    const char *turso_token;      /* Turso Bearer auth token              */
    const char *ollama_url;       /* Ollama base URL                      */
    const char *chat_model;       /* Model used for answer generation     */
    const char *embedding_model;  /* Model used for vector embeddings     */
    int         embedding_dims;   /* Dimensionality of embedding vectors  */
    int         top_k;            /* Number of chunks to retrieve per query */
    int         chunk_size;       /* Max characters per document chunk    */
} RagConfig;

/*
 * config_default — fill a RagConfig with compiled-in defaults.
 *
 * Returns a fully populated RagConfig pointing to the DEFAULT_*
 * string literals above. Safe to use on the stack — no heap
 * allocation, no cleanup required.
 */
static inline RagConfig config_default(void) {
    RagConfig cfg;
    cfg.turso_url        = DEFAULT_TURSO_URL;
    cfg.turso_token      = DEFAULT_TURSO_TOKEN;
    cfg.ollama_url       = DEFAULT_OLLAMA_URL;
    cfg.chat_model       = DEFAULT_CHAT_MODEL;
    cfg.embedding_model  = DEFAULT_EMBEDDING_MODEL;
    cfg.embedding_dims   = DEFAULT_EMBEDDING_DIMS;
    cfg.top_k            = DEFAULT_TOP_K;
    cfg.chunk_size       = DEFAULT_CHUNK_SIZE;
    return cfg;
}

#endif /* CONFIG_H */
