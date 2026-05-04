<?php

namespace RAG;

// ============================================================
// VectorSearch — Find the Most Relevant Document Chunks
// ============================================================
// When a user asks a question, we don't search by keywords.
// Instead we:
//
//   1. Convert the question into a vector (list of numbers)
//   2. Ask Turso to find the stored chunks whose vectors are
//      mathematically closest to the question vector
//   3. Return those chunks — they are the most semantically
//      similar pieces of text to what the user asked
//
// We also use a technique called QUERY EXPANSION: we generate
// a keyword-stripped version of the query and search with both.
// This improves recall — we find more relevant chunks by asking
// the question in two different ways and merging the results.
// ============================================================

class VectorSearch
{
    private Turso  $turso;
    private Ollama $ollama;
    private int    $topK;
    private ?int   $workspaceId; // null = search all, int = scope to workspace

    public function __construct(Turso $turso, Ollama $ollama, int $topK = 5, ?int $workspaceId = null)
    {
        $this->turso       = $turso;
        $this->ollama      = $ollama;
        $this->topK        = $topK;
        $this->workspaceId = $workspaceId;
    }

    // ----------------------------------------------------------
    // Search for the most relevant chunks for a given query.
    //
    // Returns an array of chunks sorted by relevance (best first),
    // each with: source, chunk_idx, content, score (0-1).
    // ----------------------------------------------------------
    public function search(string $query): array
    {
        // Step 1: Expand the query into multiple phrasings
        $queryVariants = $this->expandQuery($query);

        // Step 2: Search with each variant and collect all candidates
        $candidates = $this->searchAllVariants($queryVariants);

        // Step 3: Sort by distance (lower = more similar) and return top K
        usort($candidates, fn($a, $b) => (float) $a['distance'] <=> (float) $b['distance']);

        return array_map(
            fn($row) => [
                'source'    => $row['source'],
                'chunk_idx' => $row['chunk_idx'],
                'content'   => $row['content'],
                'score'     => round(1 - (float) $row['distance'], 4), // distance → similarity
            ],
            array_slice(array_values($candidates), 0, $this->topK)
        );
    }

    // ----------------------------------------------------------
    // Run the vector search for each query variant.
    //
    // We deduplicate by chunk ID — if the same chunk appears in
    // results from multiple variants, we keep only the best score.
    // This way a chunk that matches both variants ranks higher.
    // ----------------------------------------------------------
    private function searchAllVariants(array $queryVariants): array
    {
        $seenById = [];

        foreach ($queryVariants as $variant) {
            $embedding = $this->ollama->embed($variant);
            $vectorStr = '[' . implode(',', $embedding) . ']';

            // Fetch more than top_k so deduplication still leaves us enough
            $fetchCount = $this->topK * 2;

            // vector_top_k() is Turso's native ANN (Approximate Nearest Neighbor)
            // search. It uses the DiskANN index we built at ingest time.
            // This runs entirely inside Turso — no PHP math, no full table scan.
            $wsFilter = $this->workspaceId !== null
                ? "AND c.workspace_id = {$this->workspaceId}"
                : "";

            $rows = $this->turso->fetchAll("
                SELECT c.id, c.source, c.chunk_idx, c.content,
                       vector_distance_cos(c.embedding, vector32(?)) AS distance
                FROM vector_top_k('chunks_vector_idx', vector32(?), ?) AS v
                JOIN chunks c ON c.rowid = v.id
                WHERE 1=1 {$wsFilter}
                ORDER BY distance ASC
            ", [$vectorStr, $vectorStr, $fetchCount]);

            foreach ($rows as $row) {
                $id = $row['id'];

                // Keep the best (lowest) distance if this chunk appeared before
                $isNew       = !isset($seenById[$id]);
                $isBetter    = isset($seenById[$id]) && (float) $row['distance'] < (float) $seenById[$id]['distance'];

                if ($isNew || $isBetter) {
                    $seenById[$id] = $row;
                }
            }
        }

        return $seenById;
    }

    // ----------------------------------------------------------
    // Query expansion: search with both the original question
    // and a keyword-only version (stop words removed).
    //
    // Why: "What does OnlineCheckWriter do?" and
    //      "OnlineCheckWriter" find overlapping but different chunks.
    // Combining both gives us better overall recall.
    // ----------------------------------------------------------
    private function expandQuery(string $query): array
    {
        $variants = [$query];

        $stopWords = 'what|how|why|when|where|who|is|are|does|do|can|could|would|should|the|a|an|it|its';
        $keywords  = preg_replace("/\b($stopWords)\b/i", '', $query);
        $keywords  = trim(preg_replace('/\s+/', ' ', $keywords));

        if ($keywords !== '' && $keywords !== $query) {
            $variants[] = $keywords;
        }

        return $variants;
    }
}
