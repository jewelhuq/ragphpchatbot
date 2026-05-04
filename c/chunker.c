/*
 * chunker.c — Document chunking implementation.
 *
 * The chunking algorithm works in two phases:
 *
 *   Phase 1 — Paragraph splitting.
 *   We scan the text for double-newlines (\n\n) and split there.
 *   This gives us "paragraphs" — natural semantic units in markdown
 *   and most plain-text documents.
 *
 *   Phase 2 — Size enforcement.
 *   If a paragraph exceeds chunk_size, we subdivide it at the nearest
 *   word boundary (last space before the limit).  This avoids cutting
 *   mid-word while still respecting the size target.
 *
 *   Heading detection adds a split opportunity at heading lines even
 *   inside a long paragraph.  A heading line is:
 *     - Starts with one or more '#' characters (Markdown), OR
 *     - Is entirely uppercase and short (<= 80 chars, no punctuation)
 *       (common in README / text files).
 *
 * We accumulate paragraphs into a growing "current chunk" until adding
 * the next paragraph would push us over chunk_size.  At that point we
 * flush the current chunk and start a new one.  This paragraph-coalescing
 * strategy keeps related short paragraphs together rather than creating
 * many tiny chunks.
 *
 * Memory note: every Chunk.text is a fresh malloc.  We build strings
 * with a dynamically growing buffer (chunk_buf) and strdup them when
 * flushing.  chunk_buf is stack-allocated at a generous fixed size —
 * 4 * chunk_size is always enough because we flush before the buffer
 * overflows.
 */

#include "chunker.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Maximum paragraph size we'll accumulate into the per-chunk scratch buffer */
#define SCRATCH_SIZE (DEFAULT_CHUNK_SIZE * 4)

/* ---------------------------------------------------------------
 * Helper: detect whether a line looks like a heading.
 *
 * We look at just the first line of a paragraph (up to the first '\n').
 * A Markdown heading starts with '#'.
 * An all-caps heading is a short line with no lowercase letters.
 * --------------------------------------------------------------- */

static int is_heading(const char *para) {
    /* Markdown heading: starts with # */
    if (para[0] == '#') return 1;

    /* All-caps heading: find the first line, check it's short and uppercase */
    const char *nl = strchr(para, '\n');
    size_t line_len = nl ? (size_t)(nl - para) : strlen(para);

    if (line_len == 0 || line_len > 80) return 0;

    int has_lower = 0;
    int has_alpha = 0;
    for (size_t i = 0; i < line_len; i++) {
        if (islower((unsigned char)para[i])) { has_lower = 1; break; }
        if (isalpha((unsigned char)para[i]))   has_alpha = 1;
    }
    return (has_alpha && !has_lower);
}

/* ---------------------------------------------------------------
 * Helper: flush the current scratch buffer as a new Chunk.
 *
 * Trims leading/trailing whitespace, skips empty buffers.
 * Returns 1 if a chunk was added, 0 if skipped.
 * --------------------------------------------------------------- */

static int flush_chunk(char       *scratch,
                       int        *scratch_len,
                       const char *source,
                       Chunk      *chunks,
                       int        *chunk_count,
                       int         max_chunks)
{
    if (*scratch_len == 0) return 0;
    scratch[*scratch_len] = '\0';

    /* Trim trailing whitespace */
    int end = *scratch_len - 1;
    while (end >= 0 && (scratch[end] == '\n' || scratch[end] == ' ' || scratch[end] == '\t'))
        end--;

    if (end < 0) {
        *scratch_len = 0;
        return 0;
    }
    scratch[end + 1] = '\0';

    /* Trim leading whitespace */
    char *start = scratch;
    while (*start == '\n' || *start == ' ' || *start == '\t') start++;

    if (*start == '\0' || *chunk_count >= max_chunks) {
        *scratch_len = 0;
        return 0;
    }

    chunks[*chunk_count].text   = strdup(start);
    chunks[*chunk_count].source = strdup(source);
    chunks[*chunk_count].index  = *chunk_count;

    (*chunk_count)++;
    *scratch_len = 0;
    return 1;
}

/* ---------------------------------------------------------------
 * Helper: split a long paragraph at word boundaries.
 *
 * If para is longer than chunk_size, we emit it as multiple chunks,
 * each at most chunk_size characters, breaking at the last space
 * before the limit.
 *
 * This is the fallback for very long paragraphs (e.g. a wall of text
 * with no blank lines).
 * --------------------------------------------------------------- */

static int split_long_paragraph(const char *para,
                                const char *source,
                                Chunk      *chunks,
                                int        *chunk_count,
                                int         max_chunks,
                                int         chunk_size)
{
    size_t len    = strlen(para);
    size_t offset = 0;

    while (offset < len && *chunk_count < max_chunks) {
        size_t remaining = len - offset;
        size_t take      = (remaining > (size_t)chunk_size) ? (size_t)chunk_size : remaining;

        /* Find last space within the take window */
        if (offset + take < len) {
            size_t break_at = take;
            while (break_at > 0 && para[offset + break_at] != ' ') break_at--;
            if (break_at > 0) take = break_at;
        }

        /* Build chunk string */
        char *text = (char *)malloc(take + 1);
        if (!text) return -1;
        memcpy(text, para + offset, take);
        text[take] = '\0';

        chunks[*chunk_count].text   = text;
        chunks[*chunk_count].source = strdup(source);
        chunks[*chunk_count].index  = *chunk_count;
        (*chunk_count)++;

        /* Advance past whitespace */
        offset += take;
        while (offset < len && para[offset] == ' ') offset++;
    }
    return 0;
}

/* ---------------------------------------------------------------
 * chunk_text — public API.
 *
 * We split the input on \n\n to get paragraphs, then coalesce
 * short paragraphs into chunks.  Long paragraphs go through
 * split_long_paragraph.
 * --------------------------------------------------------------- */

int chunk_text(const char *text,
               const char *source,
               Chunk      *chunks,
               int         max_chunks,
               int         chunk_size)
{
    if (!text || !source || !chunks || max_chunks <= 0) return -1;

    int  chunk_count = 0;

    /*
     * Scratch buffer for the current accumulating chunk.
     * We allocate on the heap (chunk_size * 4 can be 6 KB).
     */
    int   scratch_cap = chunk_size * 4 + 4;
    char *scratch     = (char *)malloc(scratch_cap);
    if (!scratch) return -1;
    int   scratch_len = 0;

    /* Work on a copy so we can strtok it safely. */
    char *copy = strdup(text);
    if (!copy) { free(scratch); return -1; }

    /*
     * Split on \n\n.  strtok_r doesn't work well here because we want
     * to detect double-newlines specifically (strtok collapses runs of
     * delimiters).  Instead we scan manually.
     */
    char *p   = copy;
    char *end = copy + strlen(copy);

    while (p < end && chunk_count < max_chunks) {
        /* Find next \n\n */
        char *split = strstr(p, "\n\n");
        size_t para_len;
        char  *para_end;

        if (split) {
            para_len = (size_t)(split - p);
            para_end = split;
        } else {
            para_len = (size_t)(end - p);
            para_end = end;
        }

        /* Grab this paragraph */
        char para_save = *para_end;
        *para_end = '\0';
        const char *para = p;

        /* Skip empty paragraphs */
        if (para_len == 0 || strspn(para, " \t\n\r") == para_len) {
            *para_end = para_save;
            p = split ? split + 2 : end;
            continue;
        }

        /*
         * If the paragraph is itself a heading, flush whatever is
         * accumulating so the heading starts a fresh chunk.
         */
        if (is_heading(para) && scratch_len > 0) {
            flush_chunk(scratch, &scratch_len, source,
                        chunks, &chunk_count, max_chunks);
        }

        /*
         * If this paragraph alone exceeds chunk_size, flush current
         * accumulation and split the paragraph directly.
         */
        if ((int)para_len > chunk_size) {
            flush_chunk(scratch, &scratch_len, source,
                        chunks, &chunk_count, max_chunks);
            *para_end = para_save;
            /* Temporarily null-terminate the paragraph */
            char save2 = para_end[0];
            para_end[0] = '\0';
            split_long_paragraph(para, source, chunks, &chunk_count,
                                 max_chunks, chunk_size);
            para_end[0] = save2;
            p = split ? split + 2 : end;
            continue;
        }

        /*
         * Would adding this paragraph overflow the current chunk?
         * If so, flush first.
         */
        if (scratch_len + (int)para_len + 2 > chunk_size && scratch_len > 0) {
            flush_chunk(scratch, &scratch_len, source,
                        chunks, &chunk_count, max_chunks);
        }

        /* Append paragraph to scratch */
        if (scratch_len > 0) {
            scratch[scratch_len++] = '\n';
            scratch[scratch_len++] = '\n';
        }
        memcpy(scratch + scratch_len, para, para_len);
        scratch_len += (int)para_len;

        *para_end = para_save;
        p = split ? split + 2 : end;
    }

    /* Flush any remaining content */
    if (scratch_len > 0 && chunk_count < max_chunks) {
        flush_chunk(scratch, &scratch_len, source,
                    chunks, &chunk_count, max_chunks);
    }

    free(scratch);
    free(copy);
    return chunk_count;
}

/*
 * chunker_free_chunks — release Chunk.text and Chunk.source.
 *
 * We set each pointer to NULL after freeing so double-free is safe
 * (calling this function twice on the same array is a no-op).
 */
void chunker_free_chunks(Chunk *chunks, int count) {
    for (int i = 0; i < count; i++) {
        free(chunks[i].text);   chunks[i].text   = NULL;
        free(chunks[i].source); chunks[i].source = NULL;
    }
}
