/**
 * @file ollama.cpp
 * @brief Implementation of OllamaClient.
 *
 * The embed() path is straightforward: POST, parse, return doubles. The
 * chatStream() path is more interesting — Ollama sends newline-delimited JSON
 * where each line is a partial response object. We accumulate bytes into a
 * line buffer, parse each complete line, and call the user's callback with
 * the "message.content" delta. When we see "done": true we stop.
 */

#include "ollama.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OllamaClient::OllamaClient(const RagConfig& cfg)
    : m_cfg(cfg), m_http(120) {
    if (cfg.ollamaUrl.empty()) {
        throw std::invalid_argument("OllamaClient: ollamaUrl must not be empty");
    }
}

// ---------------------------------------------------------------------------
// embed()
// ---------------------------------------------------------------------------

std::vector<double> OllamaClient::embed(const std::string& text) {
    std::string url = m_cfg.ollamaUrl + "/api/embed";

    json body = {
        {"model", m_cfg.embedModel},
        {"input", text}
    };

    std::string raw = m_http.post(url, {}, body.dump());

    json resp;
    try {
        resp = json::parse(raw);
    } catch (const json::exception& e) {
        throw std::runtime_error(
            std::string("Ollama embed: invalid JSON response: ") + e.what()
        );
    }

    if (resp.contains("error")) {
        throw std::runtime_error(
            "Ollama embed error: " + resp["error"].get<std::string>()
        );
    }

    // Ollama /api/embed returns { "embeddings": [[...]] }
    if (!resp.contains("embeddings") || resp["embeddings"].empty()) {
        throw std::runtime_error("Ollama embed: response missing 'embeddings' array");
    }

    const auto& first = resp["embeddings"][0];
    std::vector<double> vec;
    vec.reserve(first.size());
    for (const auto& v : first) {
        vec.push_back(v.get<double>());
    }
    return vec;
}

// ---------------------------------------------------------------------------
// chatStream()
// ---------------------------------------------------------------------------

void OllamaClient::chatStream(
    const std::vector<ChatMessage>&  messages,
    std::function<void(std::string)> onChunk
) {
    std::string url = m_cfg.ollamaUrl + "/api/chat";

    // Build messages array.
    json msgs = json::array();
    for (const auto& m : messages) {
        msgs.push_back({{"role", m.role}, {"content", m.content}});
    }

    json body = {
        {"model",    m_cfg.chatModel},
        {"messages", msgs},
        {"stream",   true}
    };

    // We accumulate raw bytes from libcurl into a line buffer, then parse
    // each complete newline-terminated JSON object.
    std::string lineBuffer;

    m_http.postStream(url, {}, body.dump(),
        [&](std::string_view chunk) {
            lineBuffer.append(chunk.data(), chunk.size());

            // Extract and process all complete lines.
            std::size_t start = 0;
            while (true) {
                std::size_t nl = lineBuffer.find('\n', start);
                if (nl == std::string::npos) break;

                std::string line = lineBuffer.substr(start, nl - start);
                // Trim carriage return in case of CRLF.
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                start = nl + 1;

                if (line.empty()) continue;

                json obj;
                try {
                    obj = json::parse(line);
                } catch (...) {
                    // Partial or non-JSON line — skip.
                    continue;
                }

                if (obj.contains("error")) {
                    throw std::runtime_error(
                        "Ollama chat error: " + obj["error"].get<std::string>()
                    );
                }

                // Extract the token delta.
                if (obj.contains("message") && obj["message"].contains("content")) {
                    std::string token = obj["message"]["content"].get<std::string>();
                    if (!token.empty()) {
                        onChunk(std::move(token));
                    }
                }

                // "done": true means the model has finished generating.
                if (obj.value("done", false)) {
                    break;
                }
            }

            // Keep any bytes that follow the last complete newline.
            lineBuffer = lineBuffer.substr(start);
        }
    );
}
