/**
 * @file http.hpp
 * @brief Thin RAII wrapper around libcurl for synchronous and streaming HTTP.
 *
 * The HttpClient is the system's sole gateway to the outside world. Every
 * call to Turso and every call to Ollama passes through here. libcurl is
 * initialised once per process via the CurlGlobal RAII guard and once per
 * request via the CurlHandle RAII wrapper, so handles are never leaked even
 * when exceptions fly.
 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

/**
 * @brief RAII guard that calls curl_global_init / curl_global_cleanup.
 *
 * Construct exactly one CurlGlobal before creating any HttpClient. Its
 * destructor ensures libcurl is cleaned up when the program exits, which
 * matters for Windows where Winsock must be shut down gracefully.
 */
struct CurlGlobal {
    /**
     * @brief Initialises libcurl's global state (SSL, Winsock, etc.).
     * @throws std::runtime_error if curl_global_init fails.
     */
    CurlGlobal();

    /**
     * @brief Releases all resources acquired by curl_global_init.
     */
    ~CurlGlobal();

    // Non-copyable, non-movable — there must be exactly one.
    CurlGlobal(const CurlGlobal&)            = delete;
    CurlGlobal& operator=(const CurlGlobal&) = delete;
};

/**
 * @brief Performs HTTP POST requests, both buffered and streaming.
 *
 * HttpClient owns no persistent state beyond what libcurl needs for a single
 * request. Each call to post() or postStream() creates a fresh easy handle,
 * executes the transfer, and destroys the handle — keeping behaviour
 * predictable even from multiple threads (one client per thread).
 *
 * @note Requires that a CurlGlobal has been constructed before any method is
 *       called.
 */
class HttpClient {
public:
    /**
     * @brief Constructs an HttpClient with a configurable timeout.
     * @param timeoutSeconds Maximum seconds to wait for a complete response.
     *        Defaults to 60 s, which is generous enough for large LLM replies
     *        but short enough to surface hung connections quickly.
     */
    explicit HttpClient(long timeoutSeconds = 60);

    /**
     * @brief Sends a JSON POST request and returns the entire response body.
     *
     * The caller supplies a map of header name → value pairs. The
     * "Content-Type: application/json" header is always added automatically
     * if not already present, because every downstream service speaks JSON.
     *
     * @param url     Full URL including scheme, host, path, and query string.
     * @param headers Additional HTTP headers (e.g. Authorization).
     * @param body    Raw request body, typically a JSON string.
     * @return        The full response body as a std::string.
     * @throws std::runtime_error on any libcurl or HTTP-level error.
     */
    std::string post(
        const std::string&                        url,
        const std::map<std::string, std::string>& headers,
        const std::string&                        body
    );

    /**
     * @brief Sends a POST and delivers response chunks to a callback as they arrive.
     *
     * This is the streaming variant used for Ollama's chat endpoint, which
     * sends newline-delimited JSON objects one token at a time. The callback
     * is invoked for each chunk delivered by libcurl's write callback; chunks
     * do not necessarily correspond 1-to-1 with newlines.
     *
     * @param url      Full URL.
     * @param headers  Additional HTTP headers.
     * @param body     Request body.
     * @param onChunk  Callable invoked with each raw chunk of response data.
     *                 Receives a std::string_view that is valid only for the
     *                 duration of the call.
     * @throws std::runtime_error on any libcurl or HTTP-level error.
     */
    void postStream(
        const std::string&                        url,
        const std::map<std::string, std::string>& headers,
        const std::string&                        body,
        std::function<void(std::string_view)>     onChunk
    );

private:
    long m_timeout; ///< Transfer timeout in seconds.

    /**
     * @brief libcurl write callback — appends received bytes to a std::string.
     *
     * Registered with CURLOPT_WRITEFUNCTION. The userdata pointer points to
     * the accumulator string.
     */
    static std::size_t writeToString(
        char* ptr, std::size_t size, std::size_t nmemb, void* userdata
    );

    /**
     * @brief libcurl write callback — forwards received bytes to a lambda.
     *
     * Registered with CURLOPT_WRITEFUNCTION for streaming transfers. The
     * userdata pointer points to the std::function<void(std::string_view)>.
     */
    static std::size_t writeToCallback(
        char* ptr, std::size_t size, std::size_t nmemb, void* userdata
    );
};
