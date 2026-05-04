/*
 * chunker.h — Document chunking for the RAG ingest pipeline.
 *
 * Before storing text in Turso we split it into overlapping chunks.
 * Smaller chunks embed more precisely; larger chunks give the LLM
 * more surrounding context.  We aim for ~1500 characters each.
 *
 * Our splitting strategy (in priority order):
 *
 *   1. Paragraph boundary (\n\n) — highest quality split, preserves
 *      semantic units.  A blank line almost always marks a new idea.
 *
 *   2. Heading detection — lines that look like Markdown headings
 *      (#, ##, ###) or ALL-CAPS short lines (common in plain-text docs)
 *      become natural split points even if they're < chunk_size apart.
 *
 *   3. Hard split at chunk_size — if a paragraph is longer than
 *      chunk_size characters we split it at the nearest word boundary
 *      before the limit.
 *
 * Each chunk remembers which source file it came from so we can
 * display provenance in RAG answers.
 */

#ifndef CHUNKER_H
#define CHUNKER_H

/* A single chunk of text from a source document. */
typedef struct {
    char *text;       /* the chunk content (heap-allocated, caller frees) */
    char *source;     /* original filename (heap-allocated, caller frees)  */
    int   index;      /* 0-based chunk index within the document           */
} Chunk;

/*
 * chunk_text — split a document into overlapping chunks.
 *
 *   text       : full document text (null-terminated)
 *   source     : filename or label for this document
 *   chunks     : caller-allocated Chunk array, length max_chunks
 *   max_chunks : capacity of chunks[]
 *   chunk_size : target max characters per chunk
 *
 * Returns the number of chunks produced (<= max_chunks), or -1 on error.
 *
 * Each Chunk.text and Chunk.source is heap-allocated.
 * Caller must free them (or use chunker_free_chunks).
 */
int chunk_text(const char *text,
               const char *source,
               Chunk      *chunks,
               int         max_chunks,
               int         chunk_size);

/*
 * chunker_free_chunks — release heap memory in a chunk array.
 *
 * Frees Chunk.text and Chunk.source for the first count elements.
 * Does NOT free the chunks array itself.
 */
void chunker_free_chunks(Chunk *chunks, int count);

#endif /* CHUNKER_H */
