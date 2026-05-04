<?php

namespace RAG;

use Smalot\PdfParser\Parser as PdfParser;
use PhpOffice\PhpWord\IOFactory;

// ============================================================
// DocumentParser — Read Any Document Into Plain Text
// ============================================================
// Before we can store a document in our RAG system, we need
// its raw text. This class handles three file types:
//
//   .txt  → read directly, no processing needed
//   .pdf  → extract text from PDF structure using smalot/pdfparser
//   .docx → walk through Word document sections and collect text
//
// All three return the same thing: a plain string of text
// ready to be chunked and embedded.
// ============================================================

class DocumentParser
{
    public function parse(string $filePath): string
    {
        $extension = strtolower(pathinfo($filePath, PATHINFO_EXTENSION));

        return match ($extension) {
            'txt'  => $this->readTextFile($filePath),
            'pdf'  => $this->readPdfFile($filePath),
            'docx' => $this->readDocxFile($filePath),
            default => throw new \InvalidArgumentException(
                "Cannot parse '$extension' files. Supported types: txt, pdf, docx"
            ),
        };
    }

    // ----------------------------------------------------------
    // Plain text — just read the file as-is.
    // ----------------------------------------------------------
    private function readTextFile(string $path): string
    {
        return file_get_contents($path);
    }

    // ----------------------------------------------------------
    // PDF — PDFs store text in a binary format with layout info.
    // The PdfParser library strips all that away and returns
    // just the readable text content.
    // ----------------------------------------------------------
    private function readPdfFile(string $path): string
    {
        $parser = new PdfParser();
        $pdf    = $parser->parseFile($path);
        return $pdf->getText();
    }

    // ----------------------------------------------------------
    // DOCX — Word documents are actually ZIP files containing XML.
    // PhpWord handles unpacking and parsing the XML structure.
    // We walk through each section → element → child and collect
    // any text we find along the way.
    // ----------------------------------------------------------
    private function readDocxFile(string $path): string
    {
        $document = IOFactory::load($path);
        $text     = '';

        foreach ($document->getSections() as $section) {
            foreach ($section->getElements() as $element) {

                // Some elements are containers (paragraphs, tables)
                // that hold child elements with the actual text
                if (method_exists($element, 'getElements')) {
                    foreach ($element->getElements() as $child) {
                        if (method_exists($child, 'getText')) {
                            $text .= $child->getText() . ' ';
                        }
                    }
                }

                // Other elements directly contain text (simple runs)
                elseif (method_exists($element, 'getText')) {
                    $text .= $element->getText() . ' ';
                }
            }

            $text .= "\n"; // preserve section breaks as newlines
        }

        return $text;
    }
}
