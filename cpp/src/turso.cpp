/**
 * @file turso.cpp
 * @brief Implementation of TursoClient.
 *
 * Turso speaks a simple JSON-over-HTTP protocol. We POST a "pipeline" request
 * carrying one SQL statement, parse the response, and either return rows or
 * raise an exception. nlohmann/json handles all the serialisation so we never
 * touch raw JSON bytes by hand.
 *
 * The vector_top_k() call used by the query and RAG tools relies on Turso's
 * libSQL extension, which the cloud database already has enabled — no extra
 * setup needed.
 */

#include "turso.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TursoClient::TursoClient(const RagConfig& cfg)
    : m_cfg(cfg), m_http(60) {
    if (cfg.tursoUrl.empty()) {
        throw std::invalid_argument("TursoClient: tursoUrl must not be empty");
    }
    if (cfg.tursoToken.empty()) {
        throw std::invalid_argument("TursoClient: tursoToken must not be empty");
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::map<std::string, std::string> TursoClient::authHeaders() const {
    return {{"Authorization", "Bearer " + m_cfg.tursoToken}};
}

std::string TursoClient::buildRequestBody(
    const std::string&              sql,
    const std::vector<std::string>& params
) const {
    // Turso /v2/pipeline format:
    // { "requests": [ { "type": "execute", "stmt": { "sql": "...", "args": [...] } } ] }
    json args = json::array();
    for (const auto& p : params) {
        args.push_back({{"type", "text"}, {"value", p}});
    }

    json body = {
        {"requests", json::array({
            {
                {"type", "execute"},
                {"stmt", {
                    {"sql",  sql},
                    {"args", args}
                }}
            },
            // Always close the transaction.
            {{"type", "close"}}
        })}
    };

    return body.dump();
}

// ---------------------------------------------------------------------------
// execute()
// ---------------------------------------------------------------------------

void TursoClient::execute(
    const std::string&              sql,
    const std::vector<std::string>& params
) {
    std::string endpoint = m_cfg.tursoUrl + "/v2/pipeline";
    std::string reqBody  = buildRequestBody(sql, params);
    std::string raw      = m_http.post(endpoint, authHeaders(), reqBody);

    json resp;
    try {
        resp = json::parse(raw);
    } catch (const json::exception& e) {
        throw std::runtime_error(
            std::string("Turso response is not valid JSON: ") + e.what()
            + "\nRaw: " + raw
        );
    }

    // Check for errors in the results array.
    if (resp.contains("results")) {
        for (const auto& result : resp["results"]) {
            if (result.contains("type") && result["type"] == "error") {
                std::string msg = result.value("error", json{}).value("message", "unknown error");
                throw std::runtime_error("Turso SQL error: " + msg);
            }
        }
    }
    // Some error responses use a top-level "error" key.
    if (resp.contains("error")) {
        throw std::runtime_error("Turso error: " + resp["error"].get<std::string>());
    }
}

// ---------------------------------------------------------------------------
// fetchAll()
// ---------------------------------------------------------------------------

std::vector<std::unordered_map<std::string, std::string>> TursoClient::fetchAll(
    const std::string&              sql,
    const std::vector<std::string>& params
) {
    std::string endpoint = m_cfg.tursoUrl + "/v2/pipeline";
    std::string reqBody  = buildRequestBody(sql, params);
    std::string raw      = m_http.post(endpoint, authHeaders(), reqBody);

    json resp;
    try {
        resp = json::parse(raw);
    } catch (const json::exception& e) {
        throw std::runtime_error(
            std::string("Turso response is not valid JSON: ") + e.what()
        );
    }

    // Surface any error before attempting to parse rows.
    if (resp.contains("error")) {
        throw std::runtime_error("Turso error: " + resp["error"].get<std::string>());
    }

    std::vector<std::unordered_map<std::string, std::string>> rows;

    if (!resp.contains("results") || resp["results"].empty()) {
        return rows;
    }

    // The first result corresponds to our SELECT statement.
    const auto& first = resp["results"][0];
    if (first.contains("type") && first["type"] == "error") {
        std::string msg = first.value("error", json{}).value("message", "unknown error");
        throw std::runtime_error("Turso SQL error: " + msg);
    }

    if (!first.contains("response")) return rows;
    const auto& response = first["response"];
    if (!response.contains("result"))  return rows;
    const auto& result = response["result"];
    if (!result.contains("cols") || !result.contains("rows")) return rows;

    // Build column name list.
    std::vector<std::string> cols;
    for (const auto& col : result["cols"]) {
        cols.push_back(col.value("name", ""));
    }

    // Iterate rows.
    for (const auto& row : result["rows"]) {
        std::unordered_map<std::string, std::string> record;
        for (std::size_t i = 0; i < cols.size() && i < row.size(); ++i) {
            const auto& cell = row[i];
            std::string value;
            if (cell.is_null() || (cell.contains("type") && cell["type"] == "null")) {
                value = "NULL";
            } else if (cell.contains("value")) {
                const auto& v = cell["value"];
                if (v.is_string()) {
                    value = v.get<std::string>();
                } else if (v.is_number()) {
                    value = v.dump();
                } else if (v.is_null()) {
                    value = "NULL";
                } else {
                    value = v.dump();
                }
            } else if (cell.is_string()) {
                value = cell.get<std::string>();
            } else {
                value = cell.dump();
            }
            record[cols[i]] = value;
        }
        rows.push_back(std::move(record));
    }

    return rows;
}

// ---------------------------------------------------------------------------
// ensureSchema()
// ---------------------------------------------------------------------------

void TursoClient::ensureSchema(int dims) {
    // Main table: stores chunk text, source path, MD5 hash, and a float32
    // vector column that Turso's libSQL vector extension can index.
    std::string createTable =
        "CREATE TABLE IF NOT EXISTS " + m_cfg.tableName + " ("
        "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  source    TEXT    NOT NULL,"
        "  chunk_idx INTEGER NOT NULL,"
        "  hash      TEXT    NOT NULL,"
        "  content   TEXT    NOT NULL,"
        "  embedding F32_BLOB(" + std::to_string(dims) + ")"
        ")";
    execute(createTable);

    // Vector index — Turso's ANN index for fast nearest-neighbour lookup.
    // libsql_vector_idx is idempotent (it ignores duplicate index names).
    std::string createIndex =
        "CREATE INDEX IF NOT EXISTS " + m_cfg.tableName + "_vec_idx "
        "ON " + m_cfg.tableName + " (libsql_vector_idx(embedding))";
    execute(createIndex);
}
