/**
 * @file ingest.cpp
 * @brief Document ingestion pipeline — the tool that fills the knowledge base.
 *
 * Running this executable scans the configured docs directory, parses every
 * supported file, splits it into overlapping chunks, embeds each chunk with
 * Ollama, and stores the chunk text plus its embedding vector in Turso. It is
 * safe to re-run: existing chunks whose MD5 hash matches are skipped, and
 * chunks belonging to a file that has changed are deleted before the new
 * chunks are inserted (incremental update semantics).
 *
 * Pipeline overview:
 *  1. Ensure the database schema exists (table + vector index).
 *  2. Walk the docs directory recursively.
 *  3. For each file compute an MD5 hash via OpenSSL.
 *  4. Query the DB for existing chunks from that source.
 *  5. If all hashes match → skip (file unchanged).
 *  6. Otherwise → delete stale chunks, re-chunk, embed, insert.
 */

#include "chunker.hpp"
#include "config.hpp"
#include "ollama.hpp"
#include "parser.hpp"
#include "turso.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <openssl/md5.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// MD5 helpers
// ---------------------------------------------------------------------------

/**
 * @brief Computes the MD5 hash of a string and returns it as a hex digest.
 *
 * We use the OpenSSL EVP interface which works on both OpenSSL 1.x and 3.x.
 * The hex string is lowercase, 32 characters long.
 *
 * @param data  The string to hash.
 * @return      32-character lowercase hex string.
 */
static std::string md5Hex(const std::string& data) {
    unsigned char digest[MD5_DIGEST_LENGTH];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    // MD5() is deprecated in OpenSSL 3 but still available.
    MD5(reinterpret_cast<const unsigned char*>(data.data()),
        data.size(), digest);
#pragma GCC diagnostic pop

    std::ostringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(digest[i]);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// Vector serialisation for Turso
// ---------------------------------------------------------------------------

/**
 * @brief Formats a vector<double> as a Turso-compatible vector literal.
 *
 * Turso's vector_top_k() function accepts vectors serialised as
 * vector32('[1.0, 2.0, ...]') — a function call wrapping a JSON-array-like
 * string of floating-point numbers.
 *
 * @param vec  The embedding vector.
 * @return     A SQL expression string, e.g. "vector32('[0.1, 0.2, ...]')".
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
// Ingest a single file
// ---------------------------------------------------------------------------

/**
 * @brief Ingests one document file into the knowledge base.
 *
 * Checks whether the file's chunks are already up to date (by comparing MD5
 * hashes), deletes stale chunks, re-embeds, and inserts fresh ones.
 *
 * @param path    Filesystem path to the document.
 * @param cfg     System configuration.
 * @param db      Turso client (already connected).
 * @param ollama  Ollama client (already connected).
 * @param chunker Chunker (already configured).
 * @param parser  Document parser.
 */
static void ingestFile(
    const fs::path& path,
    const RagConfig& cfg,
    TursoClient& db,
    OllamaClient& ollama,
    const Chunker& chunker,
    const DocumentParser& parser
) {
    std::string source = path.string();
    std::cout << "[ingest] Processing: " << source << '\n';

    // Parse document content.
    std::string content;
    try {
        content = parser.parse(source);
    } catch (const std::exception& e) {
        std::cerr << "[ingest] Skipping (parse error): " << e.what() << '\n';
        return;
    }

    if (content.empty()) {
        std::cout << "[ingest] Skipping empty file.\n";
        return;
    }

    // Compute MD5 of the entire file content.
    std::string fileHash = md5Hex(content);

    // Check existing chunks for this source.
    auto existing = db.fetchAll(
        "SELECT hash FROM " + cfg.tableName + " WHERE source = ? LIMIT 1",
        {source}
    );

    if (!existing.empty()) {
        auto it = existing[0].find("hash");
        if (it != existing[0].end() && it->second == fileHash) {
            std::cout << "[ingest] Up to date, skipping.\n";
            return;
        }
        // File has changed — remove old chunks.
        std::cout << "[ingest] File changed, deleting stale chunks.\n";
        db.execute(
            "DELETE FROM " + cfg.tableName + " WHERE source = ?",
            {source}
        );
    }

    // Split into chunks.
    auto chunks = chunker.chunk(content, source);
    std::cout << "[ingest] " << chunks.size() << " chunk(s) to embed.\n";

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto& chunk = chunks[i];
        std::cout << "[ingest]   Embedding chunk " << (i + 1)
                  << " / " << chunks.size() << " ...\r" << std::flush;

        // Embed the chunk.
        std::vector<double> embedding;
        try {
            embedding = ollama.embed(chunk);
        } catch (const std::exception& e) {
            std::cerr << "\n[ingest] Embedding failed for chunk " << i
                      << ": " << e.what() << '\n';
            continue;
        }

        // Build the vector literal SQL expression.
        std::string vecExpr = vecToSql(embedding);

        // Insert via raw SQL with the vector expression inlined.
        // We can't pass the vector as a parameter because Turso's HTTP API
        // does not support binary parameters for vector32.
        std::string insertSql =
            "INSERT INTO " + cfg.tableName
            + " (source, chunk_idx, hash, content, embedding) VALUES (?, ?, ?, ?, "
            + vecExpr + ")";

        db.execute(insertSql, {source, std::to_string(i), fileHash, chunk});
    }

    std::cout << "\n[ingest] Done: " << source << '\n';
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main() {
    RagConfig cfg = RagConfig::defaults();

    std::cout << "=== RAG Ingest ===\n";
    std::cout << "Docs directory : " << cfg.docsDir << '\n';
    std::cout << "Turso URL      : " << cfg.tursoUrl << '\n';
    std::cout << "Embed model    : " << cfg.embedModel << '\n';
    std::cout << '\n';

    // Initialise libcurl once for the entire process.
    CurlGlobal curlGuard;

    TursoClient    db(cfg);
    OllamaClient   ollama(cfg);
    Chunker        chunker(cfg.chunkSize, cfg.chunkOverlap);
    DocumentParser parser;

    // Ensure schema is present before any inserts.
    std::cout << "[ingest] Ensuring database schema...\n";
    db.ensureSchema(cfg.embeddingDims);

    // Scan the docs directory.
    fs::path docsPath(cfg.docsDir);
    if (!fs::exists(docsPath)) {
        std::cerr << "[ingest] Docs directory does not exist: " << cfg.docsDir << '\n';
        std::cerr << "[ingest] Create it and add .txt or .md files, then re-run.\n";
        return 1;
    }

    std::vector<fs::path> filePaths;
    for (const auto& entry : fs::recursive_directory_iterator(docsPath)) {
        if (entry.is_regular_file()) {
            filePaths.push_back(entry.path());
        }
    }

    if (filePaths.empty()) {
        std::cout << "[ingest] No files found in " << cfg.docsDir << '\n';
        return 0;
    }

    std::cout << "[ingest] Found " << filePaths.size() << " file(s).\n\n";

    int processed = 0, skipped = 0, failed = 0;
    for (const auto& p : filePaths) {
        try {
            ingestFile(p, cfg, db, ollama, chunker, parser);
            ++processed;
        } catch (const std::exception& e) {
            std::cerr << "[ingest] Error on " << p << ": " << e.what() << '\n';
            ++failed;
        }
    }

    std::cout << "\n=== Ingest complete ===\n";
    std::cout << "Processed: " << processed << '\n';
    std::cout << "Failed   : " << failed    << '\n';

    return failed > 0 ? 1 : 0;
}
