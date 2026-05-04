/*
 * json.c — JSON helpers built on top of cJSON.
 *
 * The accessors exist purely to reduce boilerplate.  A call like:
 *
 *   const char *val = json_get_string(obj, "key");
 *
 * replaces three lines of cJSON_GetObjectItem / NULL check / ->valuestring,
 * and it reads more clearly when scanning call sites.
 *
 * The builders use the cJSON object-construction API and then call
 * cJSON_PrintUnformatted to get a compact JSON string.  We always
 * free the cJSON tree after printing — the caller only receives the
 * char* string, nothing else to manage from cJSON.
 */

#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------
 * Accessors
 * --------------------------------------------------------------- */

/*
 * json_get_string — safe string field retrieval.
 *
 * We use the case-insensitive getter because JSON from different
 * servers isn't always consistently cased (Turso uses lowercase,
 * Ollama uses lowercase too, but defensive code is good code).
 */
const char *json_get_string(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

/*
 * json_get_array — retrieve a JSON array child by key.
 *
 * Returns the cJSON node so the caller can iterate with
 * json_array_size / json_array_item or cJSON_ArrayForEach.
 */
cJSON *json_get_array(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsArray(item)) return NULL;
    return item;
}

/*
 * json_get_object — retrieve a nested JSON object child by key.
 */
cJSON *json_get_object(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (!item || !cJSON_IsObject(item)) return NULL;
    return item;
}

/*
 * json_array_size — wraps cJSON_GetArraySize with NULL guard.
 */
int json_array_size(const cJSON *arr) {
    if (!arr) return 0;
    return cJSON_GetArraySize(arr);
}

/*
 * json_array_item — wraps cJSON_GetArrayItem with bounds check.
 *
 * We don't check upper bound here (cJSON handles it gracefully by
 * returning NULL), so passing an out-of-range index is safe.
 */
cJSON *json_array_item(const cJSON *arr, int index) {
    if (!arr || index < 0) return NULL;
    return cJSON_GetArrayItem(arr, index);
}

/* ---------------------------------------------------------------
 * Request builders
 * --------------------------------------------------------------- */

/*
 * json_build_turso_request — construct the Turso pipeline HTTP body.
 *
 * Turso's HTTP API wraps every SQL statement in a "pipeline" format:
 *
 *   {
 *     "requests": [
 *       { "type": "execute",
 *         "stmt": { "sql": "INSERT ...", "args": [{"type":"text","value":"..."}] }
 *       },
 *       { "type": "close" }
 *     ]
 *   }
 *
 * The "close" step is mandatory — without it the server holds the
 * connection open waiting for more pipeline steps.
 *
 * For now we only support text-typed parameters. That covers all our
 * use cases: strings, numbers-as-strings (Turso coerces them), and
 * the base64-encoded vector blobs.
 */
char *json_build_turso_request(const char *sql,
                               const char **params,
                               int          param_count)
{
    /* Build args array */
    cJSON *args = cJSON_CreateArray();
    if (!args) return NULL;

    for (int i = 0; i < param_count; i++) {
        cJSON *arg = cJSON_CreateObject();
        if (!arg) { cJSON_Delete(args); return NULL; }
        cJSON_AddStringToObject(arg, "type",  "text");
        cJSON_AddStringToObject(arg, "value", params[i] ? params[i] : "");
        cJSON_AddItemToArray(args, arg);
    }

    /* Build the stmt object */
    cJSON *stmt = cJSON_CreateObject();
    if (!stmt) { cJSON_Delete(args); return NULL; }
    cJSON_AddStringToObject(stmt, "sql",  sql);
    cJSON_AddItemToObject(stmt, "args", args);

    /* Build the execute step */
    cJSON *exec_step = cJSON_CreateObject();
    if (!exec_step) { cJSON_Delete(stmt); return NULL; }
    cJSON_AddStringToObject(exec_step, "type", "execute");
    cJSON_AddItemToObject(exec_step, "stmt", stmt);

    /* Build the close step */
    cJSON *close_step = cJSON_CreateObject();
    if (!close_step) { cJSON_Delete(exec_step); return NULL; }
    cJSON_AddStringToObject(close_step, "type", "close");

    /* Build the requests array */
    cJSON *requests = cJSON_CreateArray();
    if (!requests) { cJSON_Delete(exec_step); cJSON_Delete(close_step); return NULL; }
    cJSON_AddItemToArray(requests, exec_step);
    cJSON_AddItemToArray(requests, close_step);

    /* Build the top-level object */
    cJSON *root = cJSON_CreateObject();
    if (!root) { cJSON_Delete(requests); return NULL; }
    cJSON_AddItemToObject(root, "requests", requests);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;  /* caller must free() */
}

/*
 * json_build_embed_request — Ollama /api/embed body.
 *
 * The embed endpoint accepts a single "input" string and returns a
 * float array in "embeddings[0]". We set "keep_alive" to 5 minutes
 * so the model stays loaded between rapid embed calls during ingest.
 */
char *json_build_embed_request(const char *model, const char *text) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddStringToObject(root, "model",      model);
    cJSON_AddStringToObject(root, "input",      text);
    cJSON_AddStringToObject(root, "keep_alive", "5m");

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/*
 * json_build_chat_request — Ollama /api/chat body.
 *
 * messages[] comes in role/content pairs:
 *   messages[0] = "system", messages[1] = "You are a helpful assistant."
 *   messages[2] = "user",   messages[3] = "What is RAG?"
 *   ...
 *
 * count must be even.  We build the messages JSON array and wrap
 * everything in the standard chat request envelope.
 *
 * stream:1 is set for chatbot.c / rag.c; query.c passes stream:0
 * when it only wants a non-streaming response (though in practice
 * we always stream, so this flag is there for flexibility).
 */
char *json_build_chat_request(const char *model,
                              const char **messages,
                              int          count,
                              int          stream)
{
    cJSON *msgs_array = cJSON_CreateArray();
    if (!msgs_array) return NULL;

    for (int i = 0; i + 1 < count; i += 2) {
        cJSON *msg = cJSON_CreateObject();
        if (!msg) { cJSON_Delete(msgs_array); return NULL; }
        cJSON_AddStringToObject(msg, "role",    messages[i]);
        cJSON_AddStringToObject(msg, "content", messages[i + 1]);
        cJSON_AddItemToArray(msgs_array, msg);
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) { cJSON_Delete(msgs_array); return NULL; }
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddItemToObject(root, "messages", msgs_array);
    cJSON_AddBoolToObject(root, "stream", stream);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
