/**
 * @file http.cpp
 * @brief Implementation of HttpClient and CurlGlobal.
 *
 * The story here is simple: we want to talk HTTP without managing buffers by
 * hand. libcurl handles the TLS handshake, chunked transfer encoding, and all
 * the other tedious protocol details. Our job is to wire up the callbacks
 * correctly and make sure every easy handle is cleaned up when we are done —
 * whether we exit normally or via an exception.
 */

#include "http.hpp"

#include <curl/curl.h>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// CurlGlobal
// ---------------------------------------------------------------------------

CurlGlobal::CurlGlobal() {
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        throw std::runtime_error(
            std::string("curl_global_init failed: ") + curl_easy_strerror(rc)
        );
    }
}

CurlGlobal::~CurlGlobal() {
    curl_global_cleanup();
}

// ---------------------------------------------------------------------------
// HttpClient
// ---------------------------------------------------------------------------

HttpClient::HttpClient(long timeoutSeconds)
    : m_timeout(timeoutSeconds) {}

// ---------------------------------------------------------------------------
// Static write callbacks
// ---------------------------------------------------------------------------

/**
 * Appends libcurl's received bytes directly to the std::string pointed to by
 * userdata. Returns the number of bytes consumed; returning anything less
 * signals an error to libcurl and aborts the transfer.
 */
std::size_t HttpClient::writeToString(
    char* ptr, std::size_t size, std::size_t nmemb, void* userdata
) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

/**
 * Invokes the user-supplied callback with a string_view over the received
 * bytes. No copy is made — the view is valid only for the duration of the
 * callback, which is fine because the callback must process the data before
 * returning.
 */
std::size_t HttpClient::writeToCallback(
    char* ptr, std::size_t size, std::size_t nmemb, void* userdata
) {
    auto* cb = static_cast<std::function<void(std::string_view)>*>(userdata);
    (*cb)(std::string_view(ptr, size * nmemb));
    return size * nmemb;
}

// ---------------------------------------------------------------------------
// Helper: build a curl_slist from a header map
// ---------------------------------------------------------------------------

/**
 * @brief Converts a std::map<string,string> into a libcurl slist.
 *
 * The caller is responsible for freeing the returned list with
 * curl_slist_free_all(). Returns nullptr for an empty map.
 */
static curl_slist* buildHeaders(
    const std::map<std::string, std::string>& headers
) {
    curl_slist* list = nullptr;
    for (const auto& [name, value] : headers) {
        std::string header = name + ": " + value;
        list = curl_slist_append(list, header.c_str());
    }
    // Always request JSON even if the caller forgot.
    list = curl_slist_append(list, "Content-Type: application/json");
    return list;
}

// ---------------------------------------------------------------------------
// post()
// ---------------------------------------------------------------------------

std::string HttpClient::post(
    const std::string&                        url,
    const std::map<std::string, std::string>& headers,
    const std::string&                        body
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() returned null");
    }

    std::string response;
    curl_slist* hdrs = buildHeaders(headers);

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        m_timeout);
    // Accept gzip/deflate if the server offers it.
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    CURLcode rc = curl_easy_perform(curl);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        throw std::runtime_error(
            std::string("HTTP POST failed: ") + curl_easy_strerror(rc)
        );
    }

    return response;
}

// ---------------------------------------------------------------------------
// postStream()
// ---------------------------------------------------------------------------

void HttpClient::postStream(
    const std::string&                        url,
    const std::map<std::string, std::string>& headers,
    const std::string&                        body,
    std::function<void(std::string_view)>     onChunk
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init() returned null");
    }

    curl_slist* hdrs = buildHeaders(headers);

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  writeToCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &onChunk);
    // Streaming calls can take a long time; use a generous timeout.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        300L);

    CURLcode rc = curl_easy_perform(curl);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        throw std::runtime_error(
            std::string("HTTP stream POST failed: ") + curl_easy_strerror(rc)
        );
    }
}
