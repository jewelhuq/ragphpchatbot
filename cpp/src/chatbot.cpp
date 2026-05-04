/**
 * @file chatbot.cpp
 * @brief Interactive multi-turn RAG chatbot with conversation history.
 *
 * The chatbot wraps the RAG pipeline in a read-eval-stream loop. On each
 * turn it:
 *  1. Reads a question from stdin (plain getline — works in pipes too).
 *  2. Embeds the question and retrieves the top-K relevant chunks.
 *  3. Appends the retrieved context to the ongoing message history.
 *  4. Streams the model's reply to stdout, token-by-token.
 *  5. Appends the reply to history so future turns can refer to it.
 *
 * History is kept in memory for the lifetime of the process. Type "exit",
 * "quit", or send EOF (Ctrl-D / Ctrl-Z) to end the session.
 *
 * The system prompt is injected once at the start of the messages array and
 * updated with fresh context on each turn so the model always has the most
 * relevant chunks in its attention window.
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
// Context retrieval
// ---------------------------------------------------------------------------

/**
 * @brief Retrieves the top-K chunks closest to the given query vector.
 *
 * @param db      Connected TursoClient.
 * @param cfg     System configuration (table name, topK).
 * @param vecExpr SQL vector32 literal expression.
 * @return        Vector of row maps, each containing at least "content".
 */
static std::vector<std::unordered_map<std::string, std::string>> retrieveChunks(
    TursoClient& db,
    const RagConfig& cfg,
    const std::string& vecExpr
) {
    std::string sql =
        "SELECT d.id, d.source, d.chunk_idx, d.content "
        "FROM vector_top_k('" + cfg.tableName + "', " + vecExpr + ", "
        + std::to_string(cfg.topK) + ") AS v "
        "JOIN " + cfg.tableName + " AS d ON d.id = v.id";

    return db.fetchAll(sql);
}

// ---------------------------------------------------------------------------
// System prompt builder
// ---------------------------------------------------------------------------

/**
 * @brief Creates a system prompt that embeds the retrieved context.
 *
 * The prompt is rebuilt every turn so the context window always holds the
 * chunks most relevant to the *current* question, not a previous one.
 *
 * @param chunks  Rows returned by retrieveChunks().
 * @return        System prompt string.
 */
static std::string buildSystemPrompt(
    const std::vector<std::unordered_map<std::string, std::string>>& chunks
) {
    std::ostringstream ss;

    ss << "You are a helpful, friendly assistant. "
          "Answer questions using ONLY the context below. "
          "If the context does not contain enough information, "
          "say so and offer to help in another way. "
          "You may refer to previous messages in this conversation.\n\n";

    if (!chunks.empty()) {
        ss << "=== RELEVANT CONTEXT ===\n";
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            auto it = chunks[i].find("content");
            if (it != chunks[i].end()) {
                auto src = chunks[i].find("source");
                if (src != chunks[i].end()) {
                    ss << "[Source: " << src->second << "]\n";
                }
                ss << it->second << "\n\n";
            }
        }
        ss << "=== END CONTEXT ===";
    } else {
        ss << "(No relevant context found in the knowledge base for this question.)";
    }

    return ss.str();
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main() {
    RagConfig cfg = RagConfig::defaults();

    CurlGlobal curlGuard;

    OllamaClient ollama(cfg);
    TursoClient  db(cfg);

    // Conversation history — grows as the session progresses.
    // Index 0 is always the system prompt, rebuilt each turn.
    std::vector<ChatMessage> history;

    std::cout << "=== RAG Chatbot ===\n";
    std::cout << "Knowledge base : " << cfg.tursoUrl << '\n';
    std::cout << "Chat model     : " << cfg.chatModel << '\n';
    std::cout << "Type 'exit' or 'quit' to end the session.\n\n";

    while (true) {
        // Prompt the user.
        std::cout << "You: " << std::flush;
        std::string userInput;
        if (!std::getline(std::cin, userInput)) {
            // EOF (Ctrl-D / Ctrl-Z) or pipe closed.
            std::cout << "\n[chatbot] Session ended.\n";
            break;
        }

        // Trim leading/trailing whitespace.
        auto ltrim = [](const std::string& s) {
            std::size_t start = s.find_first_not_of(" \t\r\n");
            return start == std::string::npos ? std::string{} : s.substr(start);
        };
        auto rtrim = [](const std::string& s) {
            std::size_t end = s.find_last_not_of(" \t\r\n");
            return end == std::string::npos ? std::string{} : s.substr(0, end + 1);
        };
        userInput = rtrim(ltrim(userInput));

        if (userInput.empty()) continue;

        // Exit commands.
        if (userInput == "exit" || userInput == "quit" ||
            userInput == "EXIT" || userInput == "QUIT") {
            std::cout << "[chatbot] Goodbye!\n";
            break;
        }

        // --- Step 1: embed the user's question ---
        std::cerr << "[chatbot] Embedding...\r" << std::flush;
        std::vector<double> queryVec;
        try {
            queryVec = ollama.embed(userInput);
        } catch (const std::exception& e) {
            std::cerr << "[chatbot] Embedding failed: " << e.what() << '\n';
            continue;
        }

        // --- Step 2: retrieve relevant context ---
        std::cerr << "[chatbot] Retrieving context...\r" << std::flush;
        std::string vecExpr = vecToSql(queryVec);
        std::vector<std::unordered_map<std::string, std::string>> chunks;
        try {
            chunks = retrieveChunks(db, cfg, vecExpr);
        } catch (const std::exception& e) {
            std::cerr << "[chatbot] Retrieval failed: " << e.what() << '\n';
            continue;
        }

        // --- Step 3: build / update the system prompt ---
        std::string systemPrompt = buildSystemPrompt(chunks);

        // Reconstruct the messages array: system prompt first, then history,
        // then the new user turn.
        std::vector<ChatMessage> messages;
        messages.push_back({"system", systemPrompt});

        // Append previous turns (skip the old system message at index 0).
        for (const auto& msg : history) {
            messages.push_back(msg);
        }

        // Add the current user message.
        messages.push_back({"user", userInput});

        // --- Step 4: stream the assistant's reply ---
        std::cerr << "                        \r" << std::flush; // Clear status line.
        std::cout << "\nAssistant: ";

        std::string assistantReply;
        try {
            ollama.chatStream(messages, [&](std::string token) {
                std::cout << token << std::flush;
                assistantReply += token;
            });
        } catch (const std::exception& e) {
            std::cerr << "\n[chatbot] Generation failed: " << e.what() << '\n';
            continue;
        }

        std::cout << "\n\n";

        // --- Step 5: append this turn to history ---
        // Store user + assistant turns (not the system prompt — it's rebuilt).
        history.push_back({"user",      userInput});
        history.push_back({"assistant", assistantReply});

        // Cap history at 20 turns (40 messages) to avoid overwhelming the
        // model's context window with very long sessions.
        while (history.size() > 40) {
            // Remove the oldest user+assistant pair.
            history.erase(history.begin(), history.begin() + 2);
        }
    }

    return 0;
}
