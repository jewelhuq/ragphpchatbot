/*
 * turso.h — Turso libSQL HTTP API client.
 *
 * Turso exposes a REST endpoint that accepts SQL statements wrapped in
 * a "pipeline" JSON envelope. We don't need the WebSocket protocol or
 * the official SDK — the HTTP API is simpler and works fine for the
 * ~tens of queries per session we make.
 *
 * The two functions here cover everything the RAG system needs:
 *
 *   turso_execute   — fire-and-forget: INSERT, CREATE TABLE, DELETE.
 *   turso_fetch_all — SELECT: returns rows as TursoRow structs.
 *
 * TursoRow holds column names and values as char* pairs.  Everything
 * is heap-allocated by turso_fetch_all and freed by turso_free_results.
 */

#ifndef TURSO_H
#define TURSO_H

#include "config.h"
#include "cJSON.h"

/* Maximum columns and string length per cell. */
#define TURSO_MAX_COLS    16
#define TURSO_MAX_STR     8192   /* per cell value — vectors can be large */

/*
 * TursoRow — one result row from a SELECT query.
 *
 * cols[i]  — the column name for column i  (heap-allocated)
 * vals[i]  — the cell value as a string     (heap-allocated)
 * ncols    — number of columns in this row
 *
 * All numeric values are returned as strings; callers convert with
 * atof/atoi as needed.  This keeps the struct simple and avoids
 * the type-dispatch complexity of a union approach.
 */
typedef struct {
    char *cols[TURSO_MAX_COLS];
    char *vals[TURSO_MAX_COLS];
    int   ncols;
} TursoRow;

/*
 * turso_execute — run a SQL statement that produces no result rows.
 *
 *   config      : RAG configuration (turso_url, turso_token)
 *   sql         : SQL statement with ? placeholders
 *   params      : NULL-terminated array of parameter strings, or NULL
 *   param_count : number of elements in params[]
 *
 * Returns 0 on success, -1 on HTTP or API error.
 * Prints an error message to stderr on failure.
 */
int turso_execute(const RagConfig *config,
                  const char      *sql,
                  const char     **params,
                  int              param_count);

/*
 * turso_fetch_all — run a SELECT and populate results[].
 *
 *   config      : RAG configuration
 *   sql         : SELECT statement with ? placeholders
 *   params      : parameter values
 *   param_count : number of parameters
 *   results     : caller-allocated array of TursoRow, length max_results
 *   max_results : capacity of results[]
 *
 * Returns the number of rows written into results[], or -1 on error.
 * Caller must call turso_free_results(results, returned_count) when done.
 */
int turso_fetch_all(const RagConfig *config,
                    const char      *sql,
                    const char     **params,
                    int              param_count,
                    TursoRow        *results,
                    int              max_results);

/*
 * turso_free_results — release heap memory in result rows.
 *
 * Frees each non-NULL cols[i] and vals[i] in the first count rows.
 * Does NOT free the results array itself (caller owns that).
 */
void turso_free_results(TursoRow *results, int count);

#endif /* TURSO_H */
