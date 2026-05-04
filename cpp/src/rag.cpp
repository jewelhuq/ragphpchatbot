/**
 * @file rag.cpp
 * @brief Single-shot RAG — embed a question, retrieve context, stream an answer.
 *
 * This is the heart of the Retrieval-Augmented Generation pipeline. The user
 * supplies a question on the command line; the program:
 *  1. Embeds the question with Ollama (nomic-embed-text).
 *  2. Finds the top-K most similar chunks in Turso (vector_top_k).
 *  3. Builds a prompt that pastes the retrieved context before the question.
 *  4. Streams the LLM's answer token-by-token to stdout.
 *
 * Usage:
 * @code
 *   ./rag "Explain the return policy in simple terms."
 * @endcode
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
// Vector serialisation
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
// Prompt construction
// ---------------------------------------------------------------------------

/**
 * @brief Assembles the RAG prompt from retrieved context chunks and the user's question.
 *
 * The prompt instructs the model to answer only from the provided context, to
 * admit ignorance when the context is insufficient, and to keep its answer
 * concise. This system prompt style has been found to reduce hallucination in
 * small models like gemma3:1b.
 *
 * @param chunks    Retrieved context chunks, ordered by similarity (closest first).
 * @param question  The user's original question.
 * @return          A fully formatted system prompt string.
 */
static std::string buildPrompt(
    const std::vector<std::unordered_map<std::string, std::string>>& chunks,
    const std::string& question
) {
    std::ostringstream ss;

    ss << "You are a helpful assistant. Answer the user's question using ONLY "
          "the context provided below. If the context does not contain enough "
          "information to answer, say so honestly.\n\n";

    ss << "=== CONTEXT ===\n";
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        auto it = chunks[i].find("content");
        if (it != chunks[i].end()) {
            ss << "--- Chunk " << (i + 1) << " ---\n"
               << it->second << "\n\n";
        }
    }
    ss << "=== END CONTEXT ===\n\n";
    ss << "Question: " << question << '\n';

    return ss.str();
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: rag <question>\n";
        std::cerr << "Example: rag \"What is the refund policy?\"\n";
        return 1;
    }

    // Join all argv tokens into one question string.
    std::string question;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) question += ' ';
        question += argv[i];
    }

    RagConfig cfg = RagConfig::defaults();

    CurlGlobal curlGuard;

    OllamaClient ollama(cfg);
    TursoClient  db(cfg);

    // --- Step 1: embed the question ---
    std::cerr << "[rag] Embedding question...\n";
    std::vector<double> queryVec;
    try {
        queryVec = ollama.embed(question);
    } catch (const std::exception& e) {
        std::cerr << "[rag] Embedding failed: " << e.what() << '\n';
        return 1;
    }

    // --- Step 2: retrieve nearest chunks ---
    std::cerr << "[rag] Searching knowledge base (top-" << cfg.topK << ")...\n";
    std::string vecExpr = vecToSql(queryVec);
    std::string sql =
        "SELECT d.id, d.source, d.chunk_idx, d.content "
        "FROM vector_top_k('" + cfg.tableName + "', " + vecExpr + ", "
        + std::to_string(cfg.topK) + ") AS v "
        "JOIN " + cfg.tableName + " AS d ON d.id = v.id";

    std::vector<std::unordered_map<std::string, std::string>> chunks;
    try {
        chunks = db.fetchAll(sql);
    } catch (const std::exception& e) {
        std::cerr << "[rag] Retrieval failed: " << e.what() << '\n';
        return 1;
    }

    if (chunks.empty()) {
        std::cerr << "[rag] No relevant chunks found. Run ingest first.\n";
        return 1;
    }

    std::cerr << "[rag] Retrieved " << chunks.size() << " chunk(s).\n\n";

    // --- Step 3: build prompt and stream answer ---
    std::string prompt = buildPrompt(chunks, question);

    std::vector<ChatMessage> messages = {
        {"system", prompt},
        {"user",   question}
    };

    std::cout << "=== Answer ===\n";
    ollama.chatStream(messages, [](std::string token) {
        std::cout << token << std::flush;
    });
    std::cout << "\n";

    return 0;
}
