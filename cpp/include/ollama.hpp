/**
 * @file ollama.hpp
 * @brief Client for the Ollama local inference server.
 *
 * Ollama is the inference engine that powers both stages of the RAG pipeline:
 * first, it turns plain text into dense embedding vectors via the
 * nomic-embed-text model; then, it generates natural-language answers via the
 * gemma3:1b chat model. OllamaClient is the thin adapter that translates
 * between C++ data types and Ollama's JSON API.
 *
 * Embeddings are returned as std::vector<double> for compatibility with
 * Turso's vector_top_k function, which expects IEEE 754 float values. Chat
 * responses are delivered token-by-token through a callback so the terminal
 * sees output as it streams in rather than waiting for the entire reply.
 */

#pragma once

#include "config.hpp"
#include "http.hpp"

#include <functional>
#include <string>
#include <vector>

/**
 * @brief Represents a single turn in a conversation.
 *
 * The role field follows OpenAI/Ollama conventions: "system", "user", or
 * "assistant". The content field holds the raw text of that turn.
 */
struct ChatMessage {
    std::string role;    ///< "system", "user", or "assistant"
    std::string content; ///< Text of this turn
};

/**
 * @brief Connects to the Ollama HTTP API for embedding and chat inference.
 *
 * Example usage:
 * @code
 *   OllamaClient ollama(RagConfig::defaults());
 *   auto vec = ollama.embed("hello world");
 *   ollama.chatStream(
 *       {{"user", "Tell me a joke."}},
 *       [](std::string tok) { std::cout << tok; }
 *   );
 * @endcode
 */
class OllamaClient {
public:
    /**
     * @brief Constructs the client from the system configuration.
     *
     * @param cfg  Configuration. The ollamaUrl, embedModel, and chatModel
     *             fields are consumed by this class.
     * @throws std::invalid_argument if ollamaUrl is empty.
     */
    explicit OllamaClient(const RagConfig& cfg);

    /**
     * @brief Generates a dense embedding vector for the given text.
     *
     * Calls POST /api/embed on the local Ollama instance. The model specified
     * in the configuration (nomic-embed-text) returns a single 768-dimensional
     * float vector. This method blocks until the embedding is ready, which is
     * typically under 100 ms on a modern CPU.
     *
     * @param text  The text to embed. Should be the chunk content or query
     *              string as-is; no special preprocessing is needed.
     * @return      A vector of doubles with length equal to embeddingDims.
     * @throws std::runtime_error on network failure or unexpected response.
     */
    std::vector<double> embed(const std::string& text);

    /**
     * @brief Sends a multi-turn conversation and streams the assistant reply.
     *
     * Calls POST /api/chat with stream=true. Ollama sends one JSON object per
     * line; each object contains a delta (one or a few tokens). This method
     * extracts the delta text from each line and forwards it to the onChunk
     * callback, allowing the caller to display tokens as they arrive.
     *
     * The conversation history is passed in full each time so Ollama can
     * attend to previous turns. Callers are responsible for maintaining the
     * messages vector across turns.
     *
     * @param messages  Full conversation history, oldest first.
     * @param onChunk   Called once per streamed token with the token text.
     *                  Must be thread-safe if called from a background thread.
     * @throws std::runtime_error on network failure or unexpected response.
     */
    void chatStream(
        const std::vector<ChatMessage>&  messages,
        std::function<void(std::string)> onChunk
    );

private:
    RagConfig  m_cfg;  ///< Copied configuration.
    HttpClient m_http; ///< Underlying HTTP transport.
};
