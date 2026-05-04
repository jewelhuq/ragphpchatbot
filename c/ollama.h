/*
 * ollama.h — Ollama local AI API client.
 *
 * Ollama runs models on localhost and exposes a simple JSON HTTP API.
 * We use two endpoints:
 *
 *   POST /api/embed  — convert text to a float vector (embedding)
 *   POST /api/chat   — generate text, streaming token by token
 *
 * This module hides the HTTP and JSON plumbing.  Callers just pass
 * text in and get floats (embed) or tokens (chat) out.
 *
 * Thread safety: these functions are NOT thread-safe.  They use
 * static buffers for the HTTP response.  All our binaries are
 * single-threaded so this is fine.
 */

#ifndef OLLAMA_H
#define OLLAMA_H

#include "config.h"
#include "http.h"

/*
 * ollama_embed — compute a text embedding vector.
 *
 * Sends the text to Ollama's /api/embed endpoint using the model
 * specified in config->embedding_model.  Writes the resulting floats
 * into embedding[], which must have room for at least dims floats.
 *
 *   config    : RAG config (ollama_url, embedding_model, embedding_dims)
 *   text      : the text to embed (null-terminated)
 *   embedding : caller-allocated float array of length >= dims
 *   dims      : number of dimensions expected (checked against response)
 *
 * Returns 0 on success, -1 on any error.
 * On success, embedding[0..dims-1] are filled with the vector values.
 */
int ollama_embed(const RagConfig *config,
                 const char      *text,
                 float           *embedding,
                 int              dims);

/*
 * ollama_chat_stream — stream a chat completion token by token.
 *
 * Sends the messages array to /api/chat with stream:true.  For each
 * JSON chunk Ollama returns, we extract the "content" field and invoke
 * the callback with the token text.
 *
 *   config   : RAG config (ollama_url, chat_model)
 *   messages : flat array of role/content string pairs
 *              [role0, content0, role1, content1, ...]
 *   count    : total number of strings (always even)
 *   callback : called per token; return non-zero to abort streaming
 *   userdata : forwarded to callback unchanged
 *
 * Returns 0 when streaming is complete, -1 on error or callback abort.
 */
int ollama_chat_stream(const RagConfig *config,
                       const char     **messages,
                       int              count,
                       StreamCallback   callback,
                       void            *userdata);

#endif /* OLLAMA_H */
