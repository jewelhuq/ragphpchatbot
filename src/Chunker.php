<?php

namespace RAG;

// ============================================================
// Chunker — Split Documents Into Digestible Pieces
// ============================================================
// An embedding model has a token limit — we can't feed it an
// entire 50-page document at once. We split the document into
// overlapping chunks that are:
//
//   - Small enough for the embedding model to process
//   - Large enough to carry meaningful context
//   - Overlapping so sentences on boundaries aren't lost
//   - Prefixed with source + section so the embedding knows
//     WHERE each piece came from, not just what it says
//
// Best practice: one idea per chunk. We split on paragraph
// boundaries and respect section headings as natural dividers.
// ============================================================

class Chunker
{
    private int $chunkSize;    // max characters per chunk
    private int $overlap;      // chars carried over to next chunk
    private int $minChunkSize; // discard fragments smaller than this

    public function __construct(int $chunkSize = 1500, int $overlap = 150, int $minChunkSize = 100)
    {
        $this->chunkSize    = $chunkSize;
        $this->overlap      = $overlap;
        $this->minChunkSize = $minChunkSize;
    }

    // ----------------------------------------------------------
    // Main entry point.
    //
    // We walk through the document paragraph by paragraph.
    // When adding the next paragraph would exceed chunkSize,
    // we save the current chunk and start a new one — but carry
    // a small overlap from the previous chunk so context isn't
    // lost at the boundary.
    //
    // $source is the document name — we prefix every chunk with
    // it so the embedding knows "this is about X" not just the
    // raw paragraph text.
    // ----------------------------------------------------------
    public function chunk(string $text, string $source = ''): array
    {
        $text       = $this->cleanText($text);
        $paragraphs = preg_split('/\n{2,}/', $text);

        $chunks         = [];
        $currentText    = '';
        $currentSection = '';

        foreach ($paragraphs as $paragraph) {
            $paragraph = trim($paragraph);
            if ($paragraph === '') continue;

            // Section headings are natural chunk boundaries.
            // When we hit one, flush whatever we've built so far
            // and start fresh under the new section.
            if ($this->looksLikeHeading($paragraph)) {
                $this->flushChunk($currentText, $source, $currentSection, $chunks);
                $currentSection = $paragraph;
                $currentText    = '';
                continue;
            }

            $textIfWeAddThisParagraph = $currentText === ''
                ? $paragraph
                : $currentText . "\n\n" . $paragraph;

            // If adding this paragraph would make the chunk too big,
            // save what we have and start a new chunk with overlap
            if (mb_strlen($textIfWeAddThisParagraph) > $this->chunkSize && $currentText !== '') {
                $this->flushChunk($currentText, $source, $currentSection, $chunks);
                $overlap     = $this->extractOverlap($currentText);
                $currentText = $overlap !== '' ? $overlap . "\n\n" . $paragraph : $paragraph;
            } else {
                $currentText = $textIfWeAddThisParagraph;
            }
        }

        // Don't forget the last chunk
        $this->flushChunk($currentText, $source, $currentSection, $chunks);

        return $chunks;
    }

    // ----------------------------------------------------------
    // Save a completed chunk to the results array.
    // We skip chunks that are too short to be meaningful
    // (leftover fragments, headers without content, etc.)
    // ----------------------------------------------------------
    private function flushChunk(string $text, string $source, string $section, array &$chunks): void
    {
        $text = trim($text);

        if (mb_strlen($text) < $this->minChunkSize) return;

        $chunks[] = $this->addContextPrefix($text, $source, $section);
    }

    // ----------------------------------------------------------
    // Prefix every chunk with source + section metadata.
    //
    // WHY: Without this, the embedding only knows the raw text.
    // With this, the embedding also captures context like:
    //   "Source: onlinecheckwriter | Section: PAIN POINTS"
    //
    // This dramatically improves retrieval accuracy because
    // queries about "pain points" will match these chunks even
    // if the chunk body doesn't repeat "pain points" explicitly.
    // ----------------------------------------------------------
    private function addContextPrefix(string $text, string $source, string $section): string
    {
        $prefix = '';

        if ($source !== '')  $prefix .= "Source: $source\n";
        if ($section !== '') $prefix .= "Section: $section\n";

        return $prefix !== '' ? trim($prefix . "\n" . $text) : $text;
    }

    // ----------------------------------------------------------
    // Pull the last sentence(s) from a chunk to use as overlap.
    //
    // WHY: If chunk 1 ends mid-thought and chunk 2 starts fresh,
    // we lose the connection. By repeating the tail of chunk 1
    // at the start of chunk 2, both chunks share that bridge.
    // ----------------------------------------------------------
    private function extractOverlap(string $text): string
    {
        if ($this->overlap <= 0) return '';

        // Take the last N*3 chars and find the first sentence boundary
        $tail = mb_substr($text, -($this->overlap * 3));

        foreach (['. ', ".\n", '? ', '! '] as $boundary) {
            $position = mb_strpos($tail, $boundary);
            if ($position !== false) {
                return trim(mb_substr($tail, $position + mb_strlen($boundary)));
            }
        }

        return trim($tail);
    }

    // ----------------------------------------------------------
    // Detect section headings so we can use them as chunk dividers.
    //
    // We look for ALL CAPS lines, markdown headings (#), and
    // numbered sections. We skip long lines — headings are short.
    // ----------------------------------------------------------
    private function looksLikeHeading(string $text): bool
    {
        if (mb_strlen($text) > 120) return false;

        $isAllCaps       = $text === mb_strtoupper($text) && mb_strlen($text) > 5;
        $isMarkdown      = (bool) preg_match('/^#{1,4}\s+/', $text);
        $isNumberedOrKey = (bool) preg_match('/^(\d+\.|PAIN POINT \d+)/i', $text);

        return $isAllCaps || $isMarkdown || $isNumberedOrKey;
    }

    // ----------------------------------------------------------
    // Clean the raw document text before chunking:
    //   - Remove page numbers ("Page 1 of 10", "- 5 -")
    //   - Remove repeated separator lines (===, ---, ***)
    //   - Collapse multiple spaces/tabs into one
    //   - Reduce 3+ blank lines down to 2
    // ----------------------------------------------------------
    private function cleanText(string $text): string
    {
        $text = preg_replace('/\bpage\s+\d+(\s+of\s+\d+)?\b/i', '', $text);
        $text = preg_replace('/^\s*-\s*\d+\s*-\s*$/m', '', $text);
        $text = preg_replace('/^[=\-\*]{3,}\s*$/m', '', $text);
        $text = preg_replace('/[ \t]+/', ' ', $text);
        $text = preg_replace('/\n{3,}/', "\n\n", $text);
        return trim($text);
    }
}
