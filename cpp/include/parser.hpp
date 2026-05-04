/**
 * @file parser.hpp
 * @brief Reads source documents and returns their content as plain UTF-8 text.
 *
 * The DocumentParser is the front door of the ingest pipeline. It inspects
 * a file's extension, chooses the appropriate reading strategy, and returns
 * the document's full text so the Chunker can process it. Text files are
 * handled natively with std::ifstream. PDF and DOCX are stubbed with clear
 * error messages that invite future implementors to plug in a real library
 * (e.g. libpoppler or the OOXML SDK).
 */

#pragma once

#include <string>

/**
 * @brief Converts a file on disk into a plain-text string.
 *
 * Usage:
 * @code
 *   DocumentParser parser;
 *   std::string text = parser.parse("docs/report.txt");
 * @endcode
 *
 * Supported formats:
 *  - .txt  — full content read via std::ifstream.
 *  - .md   — treated identically to .txt (Markdown is plain text).
 *  - .pdf  — stub: throws std::runtime_error with instructions.
 *  - .docx — stub: throws std::runtime_error with instructions.
 *  - other — stub: throws std::runtime_error.
 */
class DocumentParser {
public:
    /**
     * @brief Default constructor. The parser holds no state.
     */
    DocumentParser() = default;

    /**
     * @brief Reads the file at @p filePath and returns its content as UTF-8.
     *
     * The method dispatches on the lowercase file extension. If the file
     * cannot be opened (bad path, permission denied) a std::runtime_error is
     * thrown rather than returning an empty string, because an empty ingest
     * is a silent failure that is very hard to diagnose later.
     *
     * @param filePath  Absolute or relative path to the source document.
     * @return          Full document content as a UTF-8 string.
     * @throws std::runtime_error if the file cannot be opened or the format
     *         is not supported.
     */
    std::string parse(const std::string& filePath) const;

private:
    /**
     * @brief Reads an entire text file into a string.
     * @param filePath  Path to a .txt or .md file.
     * @return          File content.
     * @throws std::runtime_error if the file cannot be opened.
     */
    std::string parseTxt(const std::string& filePath) const;

    /**
     * @brief Stub for PDF parsing.
     *
     * Throws std::runtime_error with a message explaining how to add PDF
     * support via libpoppler-cpp.
     *
     * @param filePath  Path to a .pdf file (unused beyond the error message).
     */
    std::string parsePdf(const std::string& filePath) const;

    /**
     * @brief Stub for DOCX parsing.
     *
     * Throws std::runtime_error with a message explaining how to add DOCX
     * support via a ZIP + XML library.
     *
     * @param filePath  Path to a .docx file (unused beyond the error message).
     */
    std::string parseDocx(const std::string& filePath) const;

    /**
     * @brief Extracts and lowercases the file extension, including the dot.
     * @param filePath  Any path string.
     * @return          e.g. ".txt", ".pdf", or "" if no extension is present.
     */
    static std::string extension(const std::string& filePath);
};
