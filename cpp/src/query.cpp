/**
 * @file query.cpp
 * @brief Standalone vector similarity search tool.
 *
 * This tool answers the question "which chunks are closest to my query?"
 * without generating a natural-language answer. It is useful for debugging
 * the retrieval stage in isolation: if the wrong chunks come back here, the
 * answer in rag.cpp will be wrong regardless of how good the LLM is.
 *
 * Usage:
 * @code
 *   ./query "What is the refund policy?"
 * @endcode
 *
 * Output: a numbered list of the top-K chunks, with their source file, chunk
 * index, and content preview.
 */

#include "config.hpp"
#include "http.hpp"
#include "ollama.hpp"
#include "turso.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Vector serialisation (duplicated from ingest.cpp for standalone build)
// ---------------------------------------------------------------------------

/**
 * @brief Formats a vector<double> as a Turso vector32 literal.
 * @param vec  Embedding vector.
 * @return     SQL expression string.
 */
static std::string vecToSql(const std::vector<double>& vec) {
    std::ostringstream ss;
    ss << "vector32('[";
    for (std::size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << std::fixed << std::setprecision(8) << vec[i];
    }
    ss << "]')";
    return ss.str();
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: query <question>\n";
        std::cerr << "Example: query \"What is the return policy?\"\n";
        return 1;
    }

    // Concatenate all arguments into one query string (so the user doesn't
    // need to quote multi-word queries in every shell).
    std::string queryText;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) queryText += ' ';
        queryText += argv[i];
    }

    RagConfig cfg = RagConfig::defaults();

    std::cout << "Query  : " << queryText << '\n';
    std::cout << "Top-K  : " << cfg.topK  << '\n';
    std::cout << '\n';

    CurlGlobal curlGuard;

    OllamaClient ollama(cfg);
    TursoClient  db(cfg);

    // Step 1: embed the query.
    std::cout << "Embedding query...\n";
    std::vector<double> queryVec;
    try {
        queryVec = ollama.embed(queryText);
    } catch (const std::exception& e) {
        std::cerr << "Embedding failed: " << e.what() << '\n';
        return 1;
    }
    std::cout << "Embedding dimensions: " << queryVec.size() << '\n';

    // Step 2: vector similarity search via Turso's vector_top_k.
    // vector_top_k(table, query_vector, k) returns rowids of the nearest
    // neighbours; we JOIN back to the main table to get the content.
    std::string vecExpr = vecToSql(queryVec);
    std::string sql =
        "SELECT d.id, d.source, d.chunk_idx, d.content "
        "FROM vector_top_k('" + cfg.tableName + "', " + vecExpr + ", "
        + std::to_string(cfg.topK) + ") AS v "
        "JOIN " + cfg.tableName + " AS d ON d.id = v.id";

    std::cout << "Searching knowledge base...\n\n";
    std::vector<std::unordered_map<std::string, std::string>> rows;
    try {
        rows = db.fetchAll(sql);
    } catch (const std::exception& e) {
        std::cerr << "Search failed: " << e.what() << '\n';
        return 1;
    }

    // Step 3: display results.
    if (rows.empty()) {
        std::cout << "No results found. Have you run the ingest tool yet?\n";
        return 0;
    }

    std::cout << "=== Top " << rows.size() << " result(s) ===\n\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];

        auto get = [&](const std::string& key) -> std::string {
            auto it = row.find(key);
            return it != row.end() ? it->second : "(missing)";
        };

        std::string content = get("content");
        // Show a preview of up to 200 characters.
        std::string preview = content.size() > 200
            ? content.substr(0, 200) + "..."
            : content;

        std::cout << "--- Result " << (i + 1) << " ---\n";
        std::cout << "Source : " << get("source")    << '\n';
        std::cout << "Chunk  : " << get("chunk_idx") << '\n';
        std::cout << "Preview: " << preview           << '\n';
        std::cout << '\n';
    }

    return 0;
}
