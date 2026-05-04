/*
 * json.h — High-level JSON helpers for the RAG system.
 *
 * cJSON is a fine general-purpose parser, but calling cJSON_GetObjectItem
 * and checking for NULL everywhere creates noisy call sites. This module
 * provides three things:
 *
 *   1. Convenience accessors that return NULL / 0 safely on missing keys.
 *   2. A Turso request builder that wraps the pipeline format Turso expects.
 *   3. An Ollama request builder for embedding and chat calls.
 *
 * All functions that return char* return a pointer into the cJSON tree —
 * the caller must NOT free it. The tree owns the memory. The one exception
 * is json_build_turso_request and friends, which return malloc'd strings
 * that the caller must free().
 */

#ifndef JSON_H
#define JSON_H

#include "cJSON.h"

/* ---------------------------------------------------------------
 * Accessors — thin wrappers that handle NULL gracefully.
 * --------------------------------------------------------------- */

/*
 * json_get_string — retrieve a string field from a JSON object.
 *
 * Returns the valuestring pointer (owned by obj) or NULL if the
 * key is absent or not a string.
 */
const char *json_get_string(const cJSON *obj, const char *key);

/*
 * json_get_array — retrieve an array field from a JSON object.
 *
 * Returns the cJSON node (owned by obj) or NULL if absent / wrong type.
 */
cJSON *json_get_array(const cJSON *obj, const char *key);

/*
 * json_get_object — retrieve a nested object field.
 */
cJSON *json_get_object(const cJSON *obj, const char *key);

/*
 * json_array_size — number of elements in a JSON array.
 * Returns 0 if arr is NULL.
 */
int json_array_size(const cJSON *arr);

/*
 * json_array_item — nth element of a JSON array (0-based).
 * Returns NULL if out of range or arr is NULL.
 */
cJSON *json_array_item(const cJSON *arr, int index);

/* ---------------------------------------------------------------
 * Request builders
 * --------------------------------------------------------------- */

/*
 * json_build_turso_request — build the JSON body for a Turso HTTP API call.
 *
 * Turso's HTTP API accepts:
 *   { "requests": [ { "type": "execute",
 *                     "stmt": { "sql": "...", "args": [...] } } ,
 *                   { "type": "close" } ] }
 *
 * params[] is an array of string values that map to positional ? placeholders.
 * param_count == 0 means no parameters.
 *
 * Returns a malloc'd JSON string. Caller must free() it.
 * Returns NULL on allocation failure.
 */
char *json_build_turso_request(const char *sql,
                               const char **params,
                               int          param_count);

/*
 * json_build_embed_request — build the Ollama /api/embed request body.
 *
 * { "model": "<model>", "input": "<text>" }
 *
 * Returns malloc'd string. Caller must free().
 */
char *json_build_embed_request(const char *model, const char *text);

/*
 * json_build_chat_request — build the Ollama /api/chat request body.
 *
 * messages[] is an array of alternating role/content pairs:
 *   messages[0] = role, messages[1] = content, messages[2] = role, ...
 * count is the number of strings (always even).
 *
 * { "model": "...", "stream": true,
 *   "messages": [ {"role":"...","content":"..."}, ... ] }
 *
 * Returns malloc'd string. Caller must free().
 */
char *json_build_chat_request(const char *model,
                              const char **messages,
                              int          count,
                              int          stream);

#endif /* JSON_H */
