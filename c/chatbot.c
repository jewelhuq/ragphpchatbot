/*
 * chatbot.c — Interactive RAG chatbot with conversation history.
 *
 * Usage:
 *   ./chatbot
 *
 * This is the most complete of the four binaries.  It runs a read-
 * eval-print loop that:
 *
 *   1. Reads a line from the user (via fgets).
 *   2. Embeds the user's message.
 *   3. Searches Turso for relevant context chunks.
 *   4. Prepends those chunks to a system message.
 *   5. Appends the user's turn to a growing messages array.
 *   6. Streams the assistant's response.
 *   7. Appends the assistant's response to the history.
 *   8. Loops back to step 1.
 *
 * Multi-turn conversation works because we send the full message history
 * on every request.  The LLM sees all previous turns and can refer back
 * to them.  The context (retrieved chunks) is refreshed on every turn
 * so that follow-up questions get freshly relevant passages.
 *
 * Memory management:
 *   - messages[] is a fixed-size array of MAX_MESSAGES/2 turns.
 *   - Each message content is heap-allocated and freed when the array
 *     wraps around (oldest messages dropped to make room for new ones).
 *   - The system message (index 0) is rebuilt each turn and the old
 *     one is freed.
 *
 * Exit: type "exit", "quit", or press Ctrl-D (EOF on fgets).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "config.h"
#include "ollama.h"
#include "turso.h"

/* Maximum characters per user input line */
#define INPUT_MAX       1024

/* Maximum characters in the system/context prompt */
#define PROMPT_MAX      (64 * 1024)

/* Maximum embedding JSON string size */
#define MAX_EMBED_JSON  (768 * 16 + 16)

/*
 * Message history arrays.
 *
 * We keep parallel arrays for roles and contents.
 * msg_roles[i]    → "system", "user", or "assistant"
 * msg_contents[i] → the message text (heap-allocated, we own it)
 * msg_count       → number of messages currently in history
 *
 * We cap at MAX_MSGS.  When full, we drop the oldest user+assistant
 * pair (keeping the system message at index 0 intact).
 */
#define MAX_MSGS 64

static const char *msg_roles[MAX_MSGS];
static char       *msg_contents[MAX_MSGS];
static int         msg_count = 0;

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
 * ensure_vector_index — create ANN index if needed.
 * --------------------------------------------------------------- */

static void ensure_vector_index(const RagConfig *cfg) {
    turso_execute(cfg,
        "CREATE INDEX IF NOT EXISTS idx_documents_embedding"
        " ON documents (libsql_vector_idx(embedding))",
        NULL, 0);
}

/* ---------------------------------------------------------------
 * StreamCtx — context passed to the streaming token callback.
 *
 * We collect all streamed tokens into a buffer so we can append the
 * full assistant response to message history after streaming finishes.
 * --------------------------------------------------------------- */

typedef struct {
    char  *buf;       /* growing buffer for the full response */
    size_t len;       /* bytes written                        */
    size_t cap;       /* buffer capacity                      */
} StreamCtx;

static int token_callback(const char *data, size_t size, void *userdata) {
    StreamCtx *ctx = (StreamCtx *)userdata;

    /* Print to terminal */
    fwrite(data, 1, size, stdout);
    fflush(stdout);

    /* Accumulate into buffer */
    if (ctx->len + size + 1 > ctx->cap) {
        size_t new_cap = ctx->cap * 2 + size + 1024;
        char  *new_buf = (char *)realloc(ctx->buf, new_cap);
        if (!new_buf) return 0;  /* skip accumulation on OOM */
        ctx->buf = new_buf;
        ctx->cap = new_cap;
    }
    memcpy(ctx->buf + ctx->len, data, size);
    ctx->len += size;
    ctx->buf[ctx->len] = '\0';

    return 0;
}

/* ---------------------------------------------------------------
 * build_system_prompt — assemble context chunks into system message.
 * --------------------------------------------------------------- */

static char *build_system_prompt(TursoRow *results, int n) {
    char *buf = (char *)malloc(PROMPT_MAX);
    if (!buf) return NULL;

    int pos = 0;
    pos += snprintf(buf + pos, PROMPT_MAX - pos,
        "You are a helpful assistant. Use the context below to answer "
        "the user's questions. If the context doesn't contain the "
        "answer, say so honestly.\n\n"
        "Context:\n");

    for (int i = 0; i < n && pos < PROMPT_MAX - 256; i++) {
        const char *source  = NULL;
        const char *content = NULL;
        for (int c = 0; c < results[i].ncols; c++) {
            if (strcmp(results[i].cols[c], "source")  == 0) source  = results[i].vals[c];
            if (strcmp(results[i].cols[c], "content") == 0) content = results[i].vals[c];
        }
        if (source && content) {
            pos += snprintf(buf + pos, PROMPT_MAX - pos,
                "[%s]\n%s\n---\n", source, content);
        }
    }

    return buf;
}

/* ---------------------------------------------------------------
 * add_message — append a role/content pair to message history.
 *
 * If the array is full (at MAX_MSGS), we evict the oldest user+assistant
 * pair (indices 1 and 2, since 0 is always the system message).
 * --------------------------------------------------------------- */

static void add_message(const char *role, char *content) {
    if (msg_count >= MAX_MSGS - 1) {
        /*
         * Drop the oldest user+assistant exchange (indices 1 and 2).
         * Index 0 (system) is preserved.
         */
        free(msg_contents[1]);
        free(msg_contents[2]);
        /* Shift everything from index 3 onward down by 2 */
        for (int i = 1; i < msg_count - 2; i++) {
            msg_roles[i]    = msg_roles[i + 2];
            msg_contents[i] = msg_contents[i + 2];
        }
        msg_count -= 2;
    }
    msg_roles[msg_count]    = role;
    msg_contents[msg_count] = content;
    msg_count++;
}

/* ---------------------------------------------------------------
 * update_system_message — replace index 0 with a new system prompt.
 *
 * Called at the start of each turn with freshly retrieved context.
 * --------------------------------------------------------------- */

static void update_system_message(char *new_prompt) {
    if (msg_count > 0 && strcmp(msg_roles[0], "system") == 0) {
        free(msg_contents[0]);
        msg_contents[0] = new_prompt;
    } else {
        /* First message — insert at front by shifting */
        if (msg_count < MAX_MSGS) {
            for (int i = msg_count; i > 0; i--) {
                msg_roles[i]    = msg_roles[i - 1];
                msg_contents[i] = msg_contents[i - 1];
            }
            msg_roles[0]    = "system";
            msg_contents[0] = new_prompt;
            msg_count++;
        }
    }
}

/* ---------------------------------------------------------------
 * main — REPL loop.
 * --------------------------------------------------------------- */

int main(void) {
    printf("=== C RAG Chatbot ===\n");
    printf("Type your question and press Enter. Type 'exit' to quit.\n\n");

    curl_global_init(CURL_GLOBAL_DEFAULT);
    RagConfig cfg = config_default();

    ensure_vector_index(&cfg);

    char input[INPUT_MAX];
    float embedding[DEFAULT_EMBEDDING_DIMS];
    char  embed_json[MAX_EMBED_JSON];
    char  k_str[16];

    snprintf(k_str, sizeof k_str, "%d", cfg.top_k);

    /* Initialize the streaming accumulation buffer */
    StreamCtx stream_ctx;
    stream_ctx.cap = 4096;
    stream_ctx.buf = (char *)malloc(stream_ctx.cap);
    stream_ctx.len = 0;
    if (!stream_ctx.buf) {
        fprintf(stderr, "chatbot: out of memory\n");
        curl_global_cleanup();
        return 1;
    }

    while (1) {
        /* --- Prompt --- */
        printf("You: ");
        fflush(stdout);

        if (!fgets(input, sizeof input, stdin)) {
            printf("\n(EOF — exiting)\n");
            break;
        }

        /* Strip trailing newline */
        size_t input_len = strlen(input);
        if (input_len > 0 && input[input_len - 1] == '\n') {
            input[--input_len] = '\0';
        }
        if (input_len == 0) continue;

        /* Check exit commands */
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        }

        /* --- Embed the user's message --- */
        if (ollama_embed(&cfg, input, embedding, cfg.embedding_dims) != 0) {
            fprintf(stderr, "chatbot: embedding failed, skipping turn\n");
            continue;
        }
        embedding_to_json(embedding, cfg.embedding_dims, embed_json, sizeof embed_json);

        /* --- Retrieve context --- */
        const char *search_params[] = { embed_json, k_str };
        const char *search_sql =
            "SELECT d.source, d.content"
            " FROM vector_top_k('idx_documents_embedding', vector(?), ?)"
            " JOIN documents d ON d.id = rowid";

        TursoRow results[MAX_RESULTS];
        int n = turso_fetch_all(&cfg, search_sql, search_params, 2,
                                results, MAX_RESULTS);

        /* Build / refresh the system prompt with new context */
        char *system_prompt = NULL;
        if (n > 0) {
            system_prompt = build_system_prompt(results, n);
            turso_free_results(results, n);
        } else {
            system_prompt = strdup(
                "You are a helpful assistant. Answer the user's questions "
                "to the best of your ability.");
        }

        if (system_prompt) {
            update_system_message(system_prompt);
            /* Note: update_system_message takes ownership of system_prompt */
        }

        /* --- Append user message --- */
        char *user_content = strdup(input);
        if (!user_content) continue;
        add_message("user", user_content);

        /*
         * Build the flat messages array that ollama_chat_stream expects:
         * [role0, content0, role1, content1, ...]
         */
        const char **flat_msgs = (const char **)malloc(msg_count * 2 * sizeof(char *));
        if (!flat_msgs) continue;
        for (int i = 0; i < msg_count; i++) {
            flat_msgs[i * 2]     = msg_roles[i];
            flat_msgs[i * 2 + 1] = msg_contents[i];
        }

        /* --- Stream response --- */
        printf("\nAssistant: ");
        fflush(stdout);

        stream_ctx.len    = 0;
        stream_ctx.buf[0] = '\0';

        ollama_chat_stream(&cfg, flat_msgs, msg_count * 2,
                           token_callback, &stream_ctx);

        printf("\n\n");
        free(flat_msgs);

        /* --- Append assistant response to history --- */
        if (stream_ctx.len > 0) {
            char *assistant_content = strdup(stream_ctx.buf);
            if (assistant_content) {
                add_message("assistant", assistant_content);
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < msg_count; i++) free(msg_contents[i]);
    free(stream_ctx.buf);
    curl_global_cleanup();
    return 0;
}
