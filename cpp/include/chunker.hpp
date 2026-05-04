/**
 * @file chunker.hpp
 * @brief Splits long documents into overlapping, context-annotated chunks.
 *
 * A RAG system is only as good as its chunks. If chunks are too long they
 * overwhelm the context window; too short and they lose meaning. The Chunker
 * balances these forces by splitting on paragraph boundaries first (a natural
 * semantic unit), then merging small paragraphs until the chunk is close to
 * the target size, and finally splitting oversized paragraphs by sentence.
 *
 * Heading detection adds a context prefix to every chunk so the LLM always
 * knows which section a chunk came from — even when the chunk itself contains
 * no heading.
 */

#pragma once

#include <string>
#include <vector>

/**
 * @brief Splits a document string into overlapping chunks suitable for embedding.
 *
 * Construction is cheap; the same Chunker instance can be reused across many
 * documents.
 *
 * @code
 *   Chunker chunker(512, 64);
 *   auto chunks = chunker.chunk(documentText, "docs/manual.txt");
 * @endcode
 */
class Chunker {
public:
    /**
     * @brief Constructs a Chunker with the given size parameters.
     *
     * @param chunkSize  Desired maximum character length of each chunk (soft
     *                   limit — a chunk may exceed this if a single sentence
     *                   is longer).
     * @param overlap    Number of characters to repeat at the start of the
     *                   next chunk from the end of the current one, providing
     *                   continuity across chunk boundaries.
     */
    Chunker(int chunkSize, int overlap);

    /**
     * @brief Splits @p text into overlapping context-annotated chunks.
     *
     * The algorithm:
     *  1. Split on blank lines to get paragraphs.
     *  2. Detect Markdown-style headings (lines starting with #) and track
     *     the most recent heading as a "context prefix".
     *  3. Merge short paragraphs into a buffer until the buffer reaches
     *     chunkSize, then flush.
     *  4. Prepend the context prefix ("## Section Name\n\n") to each chunk
     *     that did not start with a heading.
     *  5. Add an overlap tail from the previous chunk to the start of the
     *     next one.
     *
     * @param text    Full document content as a UTF-8 string.
     * @param source  Human-readable identifier for the document (file path).
     *                Currently unused in the returned strings but reserved for
     *                future metadata attachment.
     * @return        Vector of chunk strings, ready to be embedded and stored.
     */
    std::vector<std::string> chunk(const std::string& text, const std::string& source) const;

private:
    int m_chunkSize; ///< Target chunk size in characters.
    int m_overlap;   ///< Overlap in characters between consecutive chunks.

    /**
     * @brief Splits a string on blank lines (one or more empty lines).
     * @param text  Input text.
     * @return      Vector of non-empty paragraph strings.
     */
    static std::vector<std::string> splitParagraphs(const std::string& text);

    /**
     * @brief Returns true if the line looks like a Markdown heading.
     * @param line  A single line of text (no newline).
     */
    static bool isHeading(const std::string& line);

    /**
     * @brief Trims leading and trailing whitespace (space, tab, CR, LF).
     * @param s  Input string.
     * @return   Trimmed copy.
     */
    static std::string trim(const std::string& s);
};
