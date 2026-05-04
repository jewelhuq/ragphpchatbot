/*
 * http.c — libcurl HTTP POST wrappers.
 *
 * Two functions, two jobs:
 *
 *   http_post        — buffer the entire response in a caller-owned char array.
 *                      Used for Turso queries where we need the full JSON blob
 *                      before we can parse it.
 *
 *   http_post_stream — fire the callback on every chunk the server sends.
 *                      Used for Ollama chat so the user sees tokens appear
 *                      as they are generated rather than waiting for the full
 *                      response.
 *
 * Both functions spin up a fresh curl easy handle per call.  The overhead is
 * negligible for our workload (tens of calls per session, not thousands), and
 * it means we never need to worry about handle state leaking between calls.
 *
 * Design note: we deliberately avoid curl_global_init/curl_global_cleanup
 * here — the caller (main) is responsible for that pair.  Every binary that
 * uses this module calls curl_global_init(CURL_GLOBAL_DEFAULT) at startup
 * and curl_global_cleanup() at exit.
 */

#include "http.h"

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------
 * Internal write-callback wiring for http_post.
 *
 * curl writes response data by calling a function we register via
 * CURLOPT_WRITEFUNCTION.  The default is fwrite to stdout, which
 * is never what we want.  We replace it with buf_write, which
 * appends bytes into a GrowBuf — a simple struct that tracks how
 * much of the caller's buffer is already used.
 * --------------------------------------------------------------- */

typedef struct {
    char   *buf;      /* points into the caller's buffer */
    size_t  used;     /* bytes written so far            */
    size_t  cap;      /* total capacity of buf           */
} GrowBuf;

/*
 * buf_write — libcurl WRITEFUNCTION for buffered calls.
 *
 * libcurl guarantees: nmemb * size bytes are valid at ptr.
 * We copy as much as will fit, always leaving room for a '\0'.
 * If the buffer is full we silently drop the overflow — callers
 * should size their buffers generously (MAX_RESPONSE_BUF = 4 MB).
 */
static size_t buf_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    GrowBuf *gb    = (GrowBuf *)userdata;
    size_t   total = size * nmemb;
    size_t   space = gb->cap - gb->used - 1;  /* reserve 1 for '\0' */

    if (space == 0) return total;  /* pretend we handled it to avoid curl abort */

    size_t copy = total < space ? total : space;
    memcpy(gb->buf + gb->used, ptr, copy);
    gb->used += copy;
    gb->buf[gb->used] = '\0';

    return total;  /* always return full amount to keep curl happy */
}

/* ---------------------------------------------------------------
 * Internal write-callback wiring for http_post_stream.
 *
 * For streaming we don't buffer at all — we pass the raw bytes
 * straight to the user's callback.  If the callback returns
 * non-zero we return 0 to curl, which causes it to abort the
 * transfer with CURLE_WRITE_ERROR.  We propagate that as -1.
 * --------------------------------------------------------------- */

typedef struct {
    StreamCallback cb;
    void          *userdata;
    int            aborted;
} StreamCtx;

/*
 * stream_write — libcurl WRITEFUNCTION for streaming calls.
 *
 * Called by curl for every chunk it deems ready (typically aligned
 * to SSL record boundaries in practice).  We forward directly to
 * the user's callback.
 */
static size_t stream_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    StreamCtx *ctx   = (StreamCtx *)userdata;
    size_t     total = size * nmemb;

    if (ctx->cb((const char *)ptr, total, ctx->userdata) != 0) {
        ctx->aborted = 1;
        return 0;  /* signal curl to abort */
    }
    return total;
}

/* ---------------------------------------------------------------
 * Shared setup — factored out to avoid repetition between the two
 * public functions.  Builds the curl_slist from the NULL-terminated
 * headers array and sets common options (POST, body, headers).
 * Returns the populated slist; caller must curl_slist_free_all it.
 * --------------------------------------------------------------- */

static struct curl_slist *build_headers(const char **headers) {
    struct curl_slist *list = NULL;
    if (!headers) return NULL;
    for (int i = 0; headers[i] != NULL; i++) {
        list = curl_slist_append(list, headers[i]);
    }
    return list;
}

/*
 * http_post — synchronous buffered POST.
 *
 * We allocate a GrowBuf pointing at the caller's buf, register
 * buf_write as the write callback, perform the transfer, then
 * ensure null termination.  Errors from curl are printed to
 * stderr so the caller gets a human-readable message even if
 * they ignore the return value.
 */
int http_post(const char *url,
              const char **headers,
              const char *body,
              char       *buf,
              size_t      buf_size)
{
    CURL             *curl;
    CURLcode          res;
    struct curl_slist *hlist;
    GrowBuf           gb;
    int               ret = 0;

    /* Zero the buffer so callers always get a valid C string. */
    if (buf_size > 0) {
        buf[0] = '\0';
    }

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "http_post: curl_easy_init failed\n");
        return -1;
    }

    gb.buf  = buf;
    gb.used = 0;
    gb.cap  = buf_size;

    hlist = build_headers(headers);

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POST,            1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,      body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,   (long)strlen(body));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,      hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   buf_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &gb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  10L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "http_post: curl error: %s\n", curl_easy_strerror(res));
        ret = -1;
    }

    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    return ret;
}

/*
 * http_post_stream — streaming POST with per-chunk callback.
 *
 * Because Ollama streams newline-delimited JSON, each chunk that
 * arrives via stream_write will be one or more complete JSON objects.
 * The callback is responsible for parsing them — we just guarantee
 * that every byte the server sent is delivered in order.
 */
int http_post_stream(const char   *url,
                     const char  **headers,
                     const char   *body,
                     StreamCallback callback,
                     void          *userdata)
{
    CURL             *curl;
    CURLcode          res;
    struct curl_slist *hlist;
    StreamCtx         ctx;
    int               ret = 0;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "http_post_stream: curl_easy_init failed\n");
        return -1;
    }

    ctx.cb       = callback;
    ctx.userdata = userdata;
    ctx.aborted  = 0;

    hlist = build_headers(headers);

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POST,            1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,      body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,   (long)strlen(body));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,      hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   stream_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         300L);  /* generous: LLMs can be slow */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  10L);

    res = curl_easy_perform(curl);

    if (ctx.aborted) {
        ret = -1;  /* callback asked us to stop */
    } else if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        fprintf(stderr, "http_post_stream: curl error: %s\n", curl_easy_strerror(res));
        ret = -1;
    }

    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    return ret;
}
