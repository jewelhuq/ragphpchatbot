/**
 * @file turso.hpp
 * @brief Client for the Turso (libSQL) HTTP API.
 *
 * Turso exposes a lightweight HTTP endpoint that accepts SQL statements
 * encoded as JSON. TursoClient is the single place in the codebase that
 * knows how to speak that protocol. Every INSERT, SELECT, CREATE TABLE, and
 * vector search goes through here.
 *
 * The client is intentionally stateless beyond the configuration it receives
 * at construction time, which makes it easy to share across threads or
 * re-create without side effects.
 */

#pragma once

#include "config.hpp"
#include "http.hpp"

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Wraps the Turso HTTP API with a clean execute/fetch interface.
 *
 * Usage pattern:
 * @code
 *   TursoClient db(RagConfig::defaults());
 *   db.execute("CREATE TABLE IF NOT EXISTS t (id INTEGER PRIMARY KEY)");
 *   auto rows = db.fetchAll("SELECT * FROM t");
 * @endcode
 */
class TursoClient {
public:
    /**
     * @brief Constructs the client and validates that the config has
     *        non-empty URL and token fields.
     *
     * @param cfg  The system-wide configuration. Only tursoUrl, tursoToken,
     *             and tableName fields are consumed by this class.
     * @throws std::invalid_argument if url or token is empty.
     */
    explicit TursoClient(const RagConfig& cfg);

    /**
     * @brief Executes a SQL statement that does not return rows (DDL / DML).
     *
     * The statement is sent as a single-statement batch to the Turso
     * /v2/pipeline endpoint. Any SQL error reported in the response body is
     * converted into a std::runtime_error so callers don't need to inspect
     * JSON themselves.
     *
     * @param sql     SQL statement, e.g. "INSERT INTO t VALUES (?)".
     * @param params  Positional parameters that replace '?' placeholders.
     *                Each element is serialised as a JSON string.
     * @throws std::runtime_error on network failure or SQL error.
     */
    void execute(
        const std::string&              sql,
        const std::vector<std::string>& params = {}
    );

    /**
     * @brief Executes a SELECT statement and returns all result rows.
     *
     * Column values are returned as strings regardless of their declared SQL
     * type. NULL values become the string "NULL". This keeps the interface
     * simple for the ingest and query tools, which only ever need string
     * comparisons and numeric parsing at the boundary.
     *
     * @param sql     SELECT statement.
     * @param params  Positional parameters.
     * @return        A vector of rows, each row a map of column name → value.
     * @throws std::runtime_error on network failure or SQL error.
     */
    std::vector<std::unordered_map<std::string, std::string>> fetchAll(
        const std::string&              sql,
        const std::vector<std::string>& params = {}
    );

    /**
     * @brief Convenience: creates the documents table and its vector index
     *        if they do not already exist.
     *
     * Idempotent — safe to call every time the ingest tool starts up.
     *
     * @param dims  Number of dimensions for the embedding column.
     * @throws std::runtime_error if any DDL statement fails.
     */
    void ensureSchema(int dims);

private:
    RagConfig   m_cfg;    ///< Copied configuration.
    HttpClient  m_http;   ///< Underlying HTTP transport.

    /**
     * @brief Builds the JSON body for a Turso /v2/pipeline request.
     *
     * Turso's pipeline format allows multiple statements per request but we
     * always send exactly one to keep error handling straightforward.
     *
     * @param sql     SQL statement.
     * @param params  Positional parameters.
     * @return        JSON string ready to POST.
     */
    std::string buildRequestBody(
        const std::string&              sql,
        const std::vector<std::string>& params
    ) const;

    /**
     * @brief Returns the Authorization header map used on every request.
     */
    std::map<std::string, std::string> authHeaders() const;
};
