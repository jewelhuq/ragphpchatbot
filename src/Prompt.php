<?php

namespace RAG;

// ============================================================
// Prompt — Build the System Prompt for the LLM
// ============================================================
// The system prompt is the instruction we give the AI before
// the user's question. It tells the AI:
//
//   - What role it plays (a precise, grounded assistant)
//   - What rules to follow (no hallucination, cite sources)
//   - What context to use (the retrieved document chunks)
//
// This is the #1 lever for controlling answer quality and
// preventing the AI from making things up.
//
// We inject the retrieved chunks directly into the prompt,
// numbered and labelled, so the AI knows exactly what it's
// allowed to draw from.
// ============================================================

class Prompt
{
    // ----------------------------------------------------------
    // Build the full system prompt from the retrieved chunks.
    //
    // Each chunk is numbered and tagged with its source file
    // and relevance score so the AI can cite where it got the
    // answer from — and so we can audit it if needed.
    // ----------------------------------------------------------
    public static function build(array $chunks): string
    {
        if (empty($chunks)) {
            return self::fallbackPrompt();
        }

        $contextBlock = self::formatChunks($chunks);

        return <<<PROMPT
You are a precise and grounded assistant. Your job is to answer questions
based on the document chunks provided below.

RULES YOU MUST FOLLOW:
1. Base your answer on the CONTEXT CHUNKS below — use them as your source of truth.
2. If the context contains the answer, use it directly and accurately.
3. If the context does NOT contain enough information, say exactly:
   "Based on the available documents, I cannot fully answer this."
   Then briefly explain what is missing.
4. NEVER invent facts, numbers, prices, or features not present in the context.
5. Be concise — avoid padding, repetition, or unnecessary filler sentences.
6. When helpful, mention which chunk or source your answer comes from.

CONTEXT CHUNKS:
$contextBlock
PROMPT;
    }

    // ----------------------------------------------------------
    // Format each chunk as a numbered, labelled block.
    //
    // Example output:
    //   [CHUNK 1 | Source: onlinecheckwriter.txt | Relevance: 0.91]
    //   OnlineCheckWriter is a cloud-based payment platform...
    // ----------------------------------------------------------
    private static function formatChunks(array $chunks): string
    {
        $formatted = '';

        foreach ($chunks as $index => $chunk) {
            $number    = $index + 1;
            $source    = $chunk['source'];
            $relevance = $chunk['score'];

            $formatted .= "[CHUNK $number | Source: $source | Relevance: $relevance]\n";
            $formatted .= $chunk['content'] . "\n\n";
        }

        return $formatted;
    }

    // ----------------------------------------------------------
    // When no relevant chunks are found at all, we still give
    // the AI a sensible instruction rather than sending nothing.
    // ----------------------------------------------------------
    private static function fallbackPrompt(): string
    {
        return "You are a helpful assistant. No relevant documents were found "
             . "for this query. Answer from your general knowledge if you can, "
             . "and clearly state that you are not drawing from any documents.";
    }
}
