/*
 * http.h — Thin libcurl wrappers for the RAG system.
 *
 * We need exactly two HTTP verbs in this codebase: POST to Turso
 * (JSON body, JSON response) and POST to Ollama (JSON body, either
 * buffered JSON or a streaming newline-delimited JSON response).
 *
 * Rather than sprinkling curl_easy_init/curl_easy_perform throughout
 * every module, we centralise the boilerplate here. Callers supply
 * a pre-allocated buffer for non-streaming calls or a callback for
 * streaming ones — we never allocate the response buffer ourselves,
 * so the caller controls memory lifetime.
 *
 * Header arrays are NULL-terminated char* arrays, matching the style
 * used everywhere else in this codebase (simple, no magic structs).
 */

#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>  /* size_t */

/*
 * StreamCallback — called by http_post_stream for each chunk of data
 * received from the server.
 *
 *   data     : pointer to the raw bytes of this chunk
 *   size     : number of bytes in this chunk
 *   userdata : the pointer passed as userdata to http_post_stream
 *
 * Return 0 to continue streaming, non-zero to abort.
 */
typedef int (*StreamCallback)(const char *data, size_t size, void *userdata);

/*
 * http_post — synchronous HTTP POST, response written into buf.
 *
 *   url      : full URL to POST to
 *   headers  : NULL-terminated array of "Header: Value" strings
 *   body     : request body (JSON string)
 *   buf      : caller-allocated buffer to receive the response body
 *   buf_size : size of buf in bytes
 *
 * Returns 0 on success, -1 on curl error.
 * On success, buf contains the null-terminated response string.
 */
int http_post(const char *url,
              const char **headers,
              const char *body,
              char       *buf,
              size_t      buf_size);

/*
 * http_post_stream — HTTP POST with server-sent / chunked streaming.
 *
 * Identical to http_post but instead of buffering the entire response,
 * we invoke callback for every chunk of data as it arrives. This is
 * how we stream Ollama tokens to the terminal without waiting for the
 * full generation to complete.
 *
 *   url      : full URL to POST to
 *   headers  : NULL-terminated array of "Header: Value" strings
 *   body     : request body (JSON string)
 *   callback : called per chunk; return non-zero to abort
 *   userdata : opaque pointer forwarded to callback unchanged
 *
 * Returns 0 on success, -1 on curl or callback-abort error.
 */
int http_post_stream(const char   *url,
                     const char  **headers,
                     const char   *body,
                     StreamCallback callback,
                     void          *userdata);

#endif /* HTTP_H */
