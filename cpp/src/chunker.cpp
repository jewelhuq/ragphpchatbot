/**
 * @file chunker.cpp
 * @brief Implementation of the document Chunker.
 *
 * The chunker's philosophy is to respect natural document structure. Blank
 * lines in prose and code are intentional signals from the author — they
 * separate ideas. We honour those signals by splitting on them first, then
 * reassemble into size-bounded chunks with a small overlap so context is not
 * severed at the boundary.
 */

#include "chunker.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Chunker::Chunker(int chunkSize, int overlap)
    : m_chunkSize(chunkSize), m_overlap(overlap) {}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::string Chunker::trim(const std::string& s) {
    const char* ws = " \t\r\n";
    std::size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) return {};
    std::size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

bool Chunker::isHeading(const std::string& line) {
    // Markdown ATX-style headings: one to six '#' characters followed by a
    // space and at least one non-whitespace character.
    std::size_t i = 0;
    while (i < line.size() && line[i] == '#') ++i;
    return i > 0 && i <= 6 && i < line.size() && line[i] == ' ' && i + 1 < line.size();
}

std::vector<std::string> Chunker::splitParagraphs(const std::string& text) {
    std::vector<std::string> paragraphs;
    std::istringstream stream(text);
    std::string line;
    std::string current;

    while (std::getline(stream, line)) {
        // Trim trailing carriage return.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) {
            // Blank line — flush current paragraph.
            std::string p = trim(current);
            if (!p.empty()) {
                paragraphs.push_back(p);
            }
            current.clear();
        } else {
            if (!current.empty()) current += '\n';
            current += line;
        }
    }

    // Flush the final paragraph if there was no trailing newline.
    std::string p = trim(current);
    if (!p.empty()) {
        paragraphs.push_back(p);
    }

    return paragraphs;
}

// ---------------------------------------------------------------------------
// chunk()
// ---------------------------------------------------------------------------

std::vector<std::string> Chunker::chunk(
    const std::string& text,
    const std::string& /*source*/
) const {
    auto paragraphs = splitParagraphs(text);
    if (paragraphs.empty()) return {};

    std::vector<std::string> result;
    std::string buffer;
    std::string lastHeading;
    std::string overlapTail; // tail of the previous chunk for continuity

    auto flushBuffer = [&]() {
        if (buffer.empty()) return;

        // Build the chunk: optional context prefix + optional overlap + buffer.
        std::string chunk;

        // If the buffer does not start with a heading, prepend the last seen
        // heading so the LLM has section context.
        bool startsWithHeading = isHeading(trim(buffer).substr(
            0, trim(buffer).find('\n') == std::string::npos
                ? trim(buffer).size()
                : trim(buffer).find('\n')
        ));

        if (!startsWithHeading && !lastHeading.empty()) {
            chunk = lastHeading + "\n\n";
        }

        // Prepend overlap from the previous chunk.
        if (!overlapTail.empty()) {
            chunk += "[...] " + overlapTail + "\n\n";
        }

        chunk += trim(buffer);
        result.push_back(chunk);

        // Compute the overlap tail for the next chunk: last m_overlap chars.
        std::string rawBuf = trim(buffer);
        if (static_cast<int>(rawBuf.size()) > m_overlap) {
            overlapTail = rawBuf.substr(rawBuf.size() - static_cast<std::size_t>(m_overlap));
        } else {
            overlapTail = rawBuf;
        }

        buffer.clear();
    };

    for (const auto& para : paragraphs) {
        // Check if this paragraph is a heading.
        std::string firstLine = para.substr(0, para.find('\n'));
        if (isHeading(trim(firstLine))) {
            lastHeading = trim(firstLine);
            // Headings anchor context but don't necessarily start a new chunk
            // on their own — they go into the buffer like any other content.
        }

        // Will adding this paragraph exceed the chunk size?
        int newSize = static_cast<int>(buffer.size())
                    + (buffer.empty() ? 0 : 2) // "\n\n" separator
                    + static_cast<int>(para.size());

        if (!buffer.empty() && newSize > m_chunkSize) {
            // Flush what we have before adding the new paragraph.
            flushBuffer();
        }

        // If the paragraph itself exceeds the chunk size, split it by
        // character boundary (sentence splitting would be more elegant but
        // requires a proper tokeniser).
        if (static_cast<int>(para.size()) > m_chunkSize) {
            int pos = 0;
            while (pos < static_cast<int>(para.size())) {
                // Try to break at a sentence boundary.
                int end = std::min(pos + m_chunkSize, static_cast<int>(para.size()));
                // Search backwards for '. ', '! ', '? '.
                if (end < static_cast<int>(para.size())) {
                    int look = end;
                    while (look > pos + m_chunkSize / 2) {
                        char c = para[look - 1];
                        if ((c == '.' || c == '!' || c == '?') &&
                            look < static_cast<int>(para.size()) && para[look] == ' ') {
                            end = look + 1;
                            break;
                        }
                        --look;
                    }
                }
                buffer = para.substr(static_cast<std::size_t>(pos),
                                     static_cast<std::size_t>(end - pos));
                flushBuffer();
                pos = end;
            }
        } else {
            if (!buffer.empty()) buffer += "\n\n";
            buffer += para;
        }
    }

    // Flush any remaining content.
    flushBuffer();

    return result;
}
