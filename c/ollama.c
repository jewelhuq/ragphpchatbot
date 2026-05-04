/*
 * ollama.c — Ollama HTTP API client implementation.
 *
 * Two functions, two different response modes:
 *
 *   ollama_embed      — buffered POST, parse the full JSON blob to extract floats.
 *   ollama_chat_stream — streaming POST, parse each NDJSON chunk to extract tokens.
 *
 * Embedding response format (/api/embed):
 *   {
 *     "model": "nomic-embed-text",
 *     "embeddings": [[0.12, -0.34, ...]],
 *     "total_duration": ...,
 *     ...
 *   }
 * The vector is in embeddings[0] (an array of numbers).
 *
 * Chat streaming response format (/api/chat, stream:true):
 * Each line is a JSON object:
 *   {"model":"gemma3:1b","created_at":"...","message":{"role":"assistant","content":"Hello"},"done":false}
 *   {"model":"gemma3:1b","created_at":"...","message":{"role":"assistant","content":""},"done":true}
 *
 * We feed the raw HTTP chunks into a line buffer, split on newlines,
 * and parse each complete line.  This handles the case where a single
 * curl write callback delivers multiple JSON lines at once.
 */

#include "ollama.h"
#include "http.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Buffer sizes */
#define OLLAMA_RESP_BUF  (2 * 1024 * 1024)   /* 2 MB for embed responses  */
#define OLLAMA_LINE_BUF  (64 * 1024)          /* 64 KB per streaming line  */

/* ---------------------------------------------------------------
 * ollama_embed — compute embedding vector.
 *
 * We build the request with json_build_embed_request, POST to
 * /api/embed, parse the response, and walk embeddings[0] to fill
 * the caller's float array.
 * --------------------------------------------------------------- */

int ollama_embed(const RagConfig *config,
                 const char      *text,
                 float           *embedding,
                 int              dims)
{
    /* Build URL: <base>/api/embed */
    char url[256];
    snprintf(url, sizeof url, "%s/api/embed", config->ollama_url);

    /* Build request body */
    char *body = json_build_embed_request(config->embedding_model, text);
    if (!body) {
        fprintf(stderr, "ollama_embed: failed to build request\n");
        return -1;
    }

    const char *headers[] = {
        "Content-Type: application/json",
        NULL
    };

    /* Allocate response buffer */
    char *resp = (char *)malloc(OLLAMA_RESP_BUF);
    if (!resp) { free(body); return -1; }

    int rc = http_post(url, headers, body, resp, OLLAMA_RESP_BUF);
    free(body);

    if (rc != 0) {
        fprintf(stderr, "ollama_embed: HTTP request failed\n");
        free(resp);
        return -1;
    }

    /* Parse response */
    cJSON *root = cJSON_Parse(resp);
    free(resp);

    if (!root) {
        fprintf(stderr, "ollama_embed: failed to parse JSON response\n");
        return -1;
    }

    /*
     * Drill into embeddings[0] — the outer array exists for batch
     * requests, but we always send a single input so index 0 is the
     * one we want.
     */
    cJSON *embeddings = json_get_array(root, "embeddings");
    if (!embeddings || json_array_size(embeddings) == 0) {
        fprintf(stderr, "ollama_embed: no 'embeddings' in response\n");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *vec = json_array_item(embeddings, 0);
    if (!vec || !cJSON_IsArray(vec)) {
        fprintf(stderr, "ollama_embed: embeddings[0] is not an array\n");
        cJSON_Delete(root);
        return -1;
    }

    int vec_len = json_array_size(vec);
    if (vec_len != dims) {
        fprintf(stderr, "ollama_embed: expected %d dims, got %d\n", dims, vec_len);
        /* We'll fill as many as fit rather than hard-failing */
        if (vec_len < dims) dims = vec_len;
    }

    /* Copy float values */
    for (int i = 0; i < dims; i++) {
        cJSON *el = json_array_item(vec, i);
        embedding[i] = el ? (float)el->valuedouble : 0.0f;
    }

    cJSON_Delete(root);
    return 0;
}

/* ---------------------------------------------------------------
 * Streaming chat — line-buffered NDJSON parser.
 *
 * Ollama sends one JSON object per line, terminated by '\n'.
 * The HTTP chunked transfer doesn't align to line boundaries, so
 * we maintain a static carry buffer across callback invocations.
 *
 * Design: we could do this with a malloc'd context struct, but a
 * static buffer is simpler and we're single-threaded anyway.
 * --------------------------------------------------------------- */

typedef struct {
    char          line_buf[OLLAMA_LINE_BUF];
    size_t        line_len;
    StreamCallback user_cb;
    void          *user_data;
    int            done;
} ChatStreamCtx;

/*
 * chat_chunk_callback — receives raw HTTP chunks from libcurl.
 *
 * We scan each incoming chunk for '\n' characters.  Everything before
 * a '\n' is appended to line_buf.  When we find a '\n' we have a
 * complete JSON line; we parse it, extract the token content, and
 * call the user callback.  Remaining bytes after the last '\n' are
 * kept in line_buf for the next chunk.
 */
static int chat_chunk_callback(const char *data, size_t size, void *userdata) {
    ChatStreamCtx *ctx = (ChatStreamCtx *)userdata;

    size_t i = 0;
    while (i < size) {
        /* Find the next newline in this chunk */
        size_t j = i;
        while (j < size && data[j] != '\n') j++;

        /* Append data[i..j) to line_buf */
        size_t seg_len = j - i;
        if (ctx->line_len + seg_len < OLLAMA_LINE_BUF - 1) {
            memcpy(ctx->line_buf + ctx->line_len, data + i, seg_len);
            ctx->line_len += seg_len;
        }
        /* else line too long: skip it gracefully */

        if (j < size && data[j] == '\n') {
            /* We have a complete line — null-terminate and parse */
            ctx->line_buf[ctx->line_len] = '\0';

            if (ctx->line_len > 0) {
                cJSON *obj = cJSON_Parse(ctx->line_buf);
                if (obj) {
                    /* Extract message.content */
                    cJSON *msg = json_get_object(obj, "message");
                    if (msg) {
                        const char *content = json_get_string(msg, "content");
                        if (content && content[0] != '\0') {
                            /* Forward to user callback */
                            if (ctx->user_cb(content, strlen(content), ctx->user_data) != 0) {
                                cJSON_Delete(obj);
                                return 1;  /* user requested abort */
                            }
                        }
                    }

                    /* Check "done" flag */
                    cJSON *done_item = cJSON_GetObjectItem(obj, "done");
                    if (done_item && cJSON_IsBool(done_item) && done_item->type == cJSON_True) {
                        ctx->done = 1;
                    }
                    cJSON_Delete(obj);
                }
            }

            /* Reset line buffer */
            ctx->line_len = 0;
            j++;  /* skip the '\n' itself */
        }

        i = j;
    }
    return 0;
}

/*
 * ollama_chat_stream — stream a chat completion.
 *
 * We build the request, set up the ChatStreamCtx line buffer, and
 * let http_post_stream drive everything through chat_chunk_callback.
 * The user_cb receives each token as a null-terminated C string.
 */
int ollama_chat_stream(const RagConfig *config,
                       const char     **messages,
                       int              count,
                       StreamCallback   callback,
                       void            *userdata)
{
    /* Build URL: <base>/api/chat */
    char url[256];
    snprintf(url, sizeof url, "%s/api/chat", config->ollama_url);

    /* Build request body with stream:true */
    char *body = json_build_chat_request(config->chat_model, messages, count, 1);
    if (!body) {
        fprintf(stderr, "ollama_chat_stream: failed to build request\n");
        return -1;
    }

    const char *headers[] = {
        "Content-Type: application/json",
        NULL
    };

    /* Set up streaming context */
    ChatStreamCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.user_cb   = callback;
    ctx.user_data = userdata;
    ctx.done      = 0;

    int rc = http_post_stream(url, headers, body, chat_chunk_callback, &ctx);
    free(body);

    return rc;
}
