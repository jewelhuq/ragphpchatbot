/**
 * @file parser.cpp
 * @brief Implementation of DocumentParser.
 *
 * Reading a text file is the simplest possible I/O: open, read everything
 * into a string, close. The PDF and DOCX branches exist as placeholders that
 * fail loudly so operators know exactly what needs to be added rather than
 * silently producing empty chunks.
 */

#include "parser.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string DocumentParser::extension(const std::string& filePath) {
    std::size_t dot = filePath.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = filePath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// ---------------------------------------------------------------------------
// parseTxt()
// ---------------------------------------------------------------------------

std::string DocumentParser::parseTxt(const std::string& filePath) const {
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("DocumentParser: cannot open file: " + filePath);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    if (file.fail() && !file.eof()) {
        throw std::runtime_error("DocumentParser: error reading file: " + filePath);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// parsePdf()
// ---------------------------------------------------------------------------

std::string DocumentParser::parsePdf(const std::string& filePath) const {
    throw std::runtime_error(
        "DocumentParser: PDF parsing is not yet implemented for: " + filePath + "\n"
        "To add PDF support, link against libpoppler-cpp and call\n"
        "poppler::document::load_from_file() here."
    );
}

// ---------------------------------------------------------------------------
// parseDocx()
// ---------------------------------------------------------------------------

std::string DocumentParser::parseDocx(const std::string& filePath) const {
    throw std::runtime_error(
        "DocumentParser: DOCX parsing is not yet implemented for: " + filePath + "\n"
        "To add DOCX support, unzip the file (it is a ZIP archive) and parse\n"
        "word/document.xml with a SAX or DOM XML library."
    );
}

// ---------------------------------------------------------------------------
// parse() — dispatch on extension
// ---------------------------------------------------------------------------

std::string DocumentParser::parse(const std::string& filePath) const {
    std::string ext = extension(filePath);

    if (ext == ".txt" || ext == ".md" || ext == ".rst") {
        return parseTxt(filePath);
    } else if (ext == ".pdf") {
        return parsePdf(filePath);
    } else if (ext == ".docx") {
        return parseDocx(filePath);
    } else {
        throw std::runtime_error(
            "DocumentParser: unsupported file extension '" + ext
            + "' for file: " + filePath
        );
    }
}
