/*
 * turso.c — Turso libSQL HTTP API implementation.
 *
 * Turso's HTTP pipeline API works like this:
 *
 *   POST /v2/pipeline
 *   Authorization: Bearer <token>
 *   Content-Type: application/json
 *
 *   {
 *     "requests": [
 *       { "type": "execute", "stmt": { "sql": "...", "args": [...] } },
 *       { "type": "close" }
 *     ]
 *   }
 *
 * The response looks like:
 *
 *   {
 *     "results": [
 *       { "type": "ok",
 *         "response": {
 *           "type": "execute",
 *           "result": {
 *             "cols": [ {"name": "id", "decltype": "INTEGER"}, ... ],
 *             "rows": [ [ {"type":"integer","value":"1"}, ... ], ... ]
 *           }
 *         }
 *       },
 *       { "type": "ok", "response": { "type": "close" } }
 *     ]
 *   }
 *
 * We parse this response to build TursoRow arrays.  The nested
 * structure is a bit deep but mechanical — follow the keys down and
 * check for "error" type at each step.
 *
 * URL building: we append "/v2/pipeline" to turso_url ourselves so
 * config.h only needs to store the base URL (cleaner for the user).
 */

#include "turso.h"
#include "http.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Size of the HTTP response buffer used for Turso calls.
 * Vector rows can be large (768 floats * ~10 chars each = ~8 KB per row,
 * times top_k rows plus overhead), so 4 MB is generous but safe. */
#define TURSO_BUF_SIZE (4 * 1024 * 1024)

/* ---------------------------------------------------------------
 * Internal: build the pipeline URL from the base URL.
 *
 * We write into a stack buffer.  The base URL is at most a few hundred
 * bytes, so 512 is always enough.
 * --------------------------------------------------------------- */

static void build_pipeline_url(const RagConfig *cfg, char *buf, size_t buf_len) {
    snprintf(buf, buf_len, "%s/v2/pipeline", cfg->turso_url);
}

/* ---------------------------------------------------------------
 * Internal: build the Authorization and Content-Type header array.
 *
 * Returned as a NULL-terminated static-duration array — the caller
 * must NOT free it, but it IS overwritten on each call.  This is
 * safe because our call sites are single-threaded.
 * --------------------------------------------------------------- */

static const char *build_headers(const RagConfig *cfg,
                                 char             auth_buf[],
                                 size_t           auth_buf_len)
{
    snprintf(auth_buf, auth_buf_len, "Authorization: Bearer %s", cfg->turso_token);
    return auth_buf;  /* used by the fixed headers array below */
}

/* ---------------------------------------------------------------
 * Internal: perform one HTTP POST to the pipeline endpoint.
 *
 * Fills resp_buf with the raw JSON response.
 * Returns 0 on success, -1 on HTTP error.
 * --------------------------------------------------------------- */

static int turso_post(const RagConfig *cfg,
                      const char      *body,
                      char            *resp_buf,
                      size_t           resp_buf_len)
{
    char url[512];
    build_pipeline_url(cfg, url, sizeof url);

    char auth_buf[1024];
    snprintf(auth_buf, sizeof auth_buf,
             "Authorization: Bearer %s", cfg->turso_token);

    const char *headers[] = {
        auth_buf,
        "Content-Type: application/json",
        NULL
    };

    return http_post(url, headers, body, resp_buf, resp_buf_len);
}

/* ---------------------------------------------------------------
 * Internal: parse results[0].response.result from the pipeline response.
 *
 * Returns the "result" cJSON object, or NULL if the first pipeline
 * step errored.  Also prints a descriptive error message on failure.
 * --------------------------------------------------------------- */

static cJSON *extract_result(const char *resp_buf) {
    cJSON *root = cJSON_Parse(resp_buf);
    if (!root) {
        fprintf(stderr, "turso: failed to parse JSON response\n");
        return NULL;
    }

    cJSON *results = json_get_array(root, "results");
    if (!results || json_array_size(results) == 0) {
        fprintf(stderr, "turso: no 'results' array in response\n");
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *first = json_array_item(results, 0);
    if (!first) { cJSON_Delete(root); return NULL; }

    /* Check for error type */
    const char *type = json_get_string(first, "type");
    if (type && strcmp(type, "error") == 0) {
        const char *msg = json_get_string(first, "error");
        fprintf(stderr, "turso: API error: %s\n", msg ? msg : "(unknown)");
        cJSON_Delete(root);
        return NULL;
    }

    /* Drill down: results[0].response.result */
    cJSON *response = json_get_object(first, "response");
    if (!response) {
        /* no rows, probably a DDL statement */
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *result = json_get_object(response, "result");
    if (!result) {
        /* fire-and-forget: no result object is fine */
        cJSON_Delete(root);
        return NULL;
    }

    /* Detach result so we can delete the root tree and return just result.
     * We do a full duplicate because DetachItemViaPointer requires parent. */
    cJSON *dup = cJSON_Duplicate(result, 1);
    cJSON_Delete(root);
    return dup;  /* caller must cJSON_Delete(dup) */
}

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */

/*
 * turso_execute — fire-and-forget SQL execution.
 *
 * We build the request, POST it, parse just enough to check for an
 * error response, and return.  For INSERT/UPDATE/CREATE TABLE there
 * are no rows to process.
 */
int turso_execute(const RagConfig *config,
                  const char      *sql,
                  const char     **params,
                  int              param_count)
{
    char *body = json_build_turso_request(sql, params, param_count);
    if (!body) {
        fprintf(stderr, "turso_execute: failed to build request JSON\n");
        return -1;
    }

    char *resp_buf = (char *)malloc(TURSO_BUF_SIZE);
    if (!resp_buf) {
        free(body);
        fprintf(stderr, "turso_execute: out of memory\n");
        return -1;
    }

    int rc = turso_post(config, body, resp_buf, TURSO_BUF_SIZE);
    free(body);

    if (rc != 0) {
        free(resp_buf);
        return -1;
    }

    /* Parse enough to catch API-level errors */
    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);

    if (!root) {
        fprintf(stderr, "turso_execute: invalid JSON response\n");
        return -1;
    }

    /* Check results[0].type == "error" */
    cJSON *results = json_get_array(root, "results");
    if (results) {
        cJSON *first = json_array_item(results, 0);
        if (first) {
            const char *type = json_get_string(first, "type");
            if (type && strcmp(type, "error") == 0) {
                const char *msg = json_get_string(first, "error");
                fprintf(stderr, "turso_execute error: %s\n", msg ? msg : "(unknown)");
                cJSON_Delete(root);
                return -1;
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

/*
 * turso_fetch_all — SELECT and populate TursoRow array.
 *
 * The inner loop mirrors the Turso response structure:
 *   result.cols[] → column names (array of {name, decltype})
 *   result.rows[] → array of rows, each row is array of {type, value}
 *
 * We allocate strdup copies of every string so TursoRow doesn't
 * depend on the lifetime of the cJSON tree.  turso_free_results()
 * cleans up those copies.
 */
int turso_fetch_all(const RagConfig *config,
                    const char      *sql,
                    const char     **params,
                    int              param_count,
                    TursoRow        *results,
                    int              max_results)
{
    char *body = json_build_turso_request(sql, params, param_count);
    if (!body) {
        fprintf(stderr, "turso_fetch_all: failed to build request\n");
        return -1;
    }

    char *resp_buf = (char *)malloc(TURSO_BUF_SIZE);
    if (!resp_buf) { free(body); return -1; }

    int rc = turso_post(config, body, resp_buf, TURSO_BUF_SIZE);
    free(body);

    if (rc != 0) { free(resp_buf); return -1; }

    /* Parse the result object (a duplicate, we own it) */
    cJSON *result = extract_result(resp_buf);
    free(resp_buf);

    if (!result) {
        /* Could be a DDL with no result object — return 0 rows, not error */
        return 0;
    }

    /* cols array: [ {"name": "id", "decltype": "INTEGER"}, ... ] */
    cJSON *cols_json = json_get_array(result, "cols");
    int    ncols     = json_array_size(cols_json);
    if (ncols > TURSO_MAX_COLS) ncols = TURSO_MAX_COLS;

    /* rows array: [ [ {"type":"integer","value":"1"}, ... ], ... ] */
    cJSON *rows_json = json_get_array(result, "rows");
    int    nrows     = json_array_size(rows_json);
    if (nrows > max_results) nrows = max_results;

    /* Populate column name list once — shared by all rows */
    const char *col_names[TURSO_MAX_COLS];
    for (int c = 0; c < ncols; c++) {
        cJSON *col_obj = json_array_item(cols_json, c);
        col_names[c] = col_obj ? json_get_string(col_obj, "name") : "";
        if (!col_names[c]) col_names[c] = "";
    }

    /* Walk rows */
    int row_count = 0;
    for (int r = 0; r < nrows; r++) {
        cJSON *row_json = json_array_item(rows_json, r);
        if (!row_json || !cJSON_IsArray(row_json)) continue;

        TursoRow *row = &results[row_count];
        row->ncols = 0;

        int cell_count = json_array_size(row_json);
        if (cell_count > ncols) cell_count = ncols;

        for (int c = 0; c < cell_count; c++) {
            cJSON *cell = json_array_item(row_json, c);
            const char *val = cell ? json_get_string(cell, "value") : NULL;

            row->cols[c] = strdup(col_names[c]);
            row->vals[c] = strdup(val ? val : "");

            if (!row->cols[c] || !row->vals[c]) {
                /* Partial allocation — clean up and fail */
                for (int k = 0; k <= c; k++) {
                    free(row->cols[k]); row->cols[k] = NULL;
                    free(row->vals[k]); row->vals[k] = NULL;
                }
                cJSON_Delete(result);
                return -1;
            }
        }
        row->ncols = cell_count;
        row_count++;
    }

    cJSON_Delete(result);
    return row_count;
}

/*
 * turso_free_results — release heap allocations in result rows.
 *
 * Each TursoRow has up to TURSO_MAX_COLS strdup'd strings.  We free
 * every non-NULL pointer.  The TursoRow array itself (stack or caller-
 * heap) is left to the caller.
 */
void turso_free_results(TursoRow *results, int count) {
    for (int r = 0; r < count; r++) {
        for (int c = 0; c < results[r].ncols; c++) {
            free(results[r].cols[c]); results[r].cols[c] = NULL;
            free(results[r].vals[c]); results[r].vals[c] = NULL;
        }
        results[r].ncols = 0;
    }
}
