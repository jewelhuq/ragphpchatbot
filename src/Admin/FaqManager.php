<?php

namespace RAG\Admin;

use RAG\Turso;

// ============================================================
// FaqManager — CRUD for FAQ Items per Workspace
// ============================================================
// FAQ items are NOT connected to the RAG pipeline.
// They are manual Q&A pairs shown in the widget's FAQ tab.
// Answers are stored as Markdown and rendered in the widget.
//
// Fuzzy search is implemented in PHP using similar_text()
// since Turso/SQLite doesn't have built-in fuzzy matching.
// ============================================================

class FaqManager
{
    private Turso $turso;

    public function __construct(Turso $turso)
    {
        $this->turso = $turso;
    }

    // ----------------------------------------------------------
    // Get all FAQ items for a workspace, ordered by sort_order.
    // ----------------------------------------------------------
    public function list(int $workspaceId): array
    {
        return $this->turso->fetchAll(
            "SELECT * FROM faq WHERE workspace_id = ? ORDER BY sort_order ASC, id ASC",
            [$workspaceId]
        );
    }

    // ----------------------------------------------------------
    // Create a new FAQ item.
    // ----------------------------------------------------------
    public function create(int $workspaceId, string $question, string $answer): array
    {
        $question = trim($question);
        $answer   = trim($answer);

        if ($question === '') throw new \InvalidArgumentException("Question cannot be empty.");
        if ($answer === '')   throw new \InvalidArgumentException("Answer cannot be empty.");

        // Place new items at the end
        $maxOrder = $this->turso->fetchAll(
            "SELECT MAX(sort_order) as max_order FROM faq WHERE workspace_id = ?",
            [$workspaceId]
        );
        $nextOrder = ((int)($maxOrder[0]['max_order'] ?? 0)) + 1;

        $this->turso->execute(
            "INSERT INTO faq (workspace_id, question, answer, sort_order) VALUES (?, ?, ?, ?)",
            [$workspaceId, $question, $answer, $nextOrder]
        );

        return $this->turso->fetchAll(
            "SELECT * FROM faq WHERE workspace_id = ? ORDER BY id DESC LIMIT 1",
            [$workspaceId]
        )[0];
    }

    // ----------------------------------------------------------
    // Update an existing FAQ item.
    // ----------------------------------------------------------
    public function update(int $faqId, int $workspaceId, string $question, string $answer): void
    {
        $question = trim($question);
        $answer   = trim($answer);

        if ($question === '') throw new \InvalidArgumentException("Question cannot be empty.");
        if ($answer === '')   throw new \InvalidArgumentException("Answer cannot be empty.");

        $this->turso->execute(
            "UPDATE faq SET question = ?, answer = ?, updated_at = datetime('now')
             WHERE id = ? AND workspace_id = ?",
            [$question, $answer, $faqId, $workspaceId]
        );
    }

    // ----------------------------------------------------------
    // Delete a FAQ item.
    // ----------------------------------------------------------
    public function delete(int $faqId, int $workspaceId): void
    {
        $this->turso->execute(
            "DELETE FROM faq WHERE id = ? AND workspace_id = ?",
            [$faqId, $workspaceId]
        );
    }

    // ----------------------------------------------------------
    // Reorder FAQ items by passing an ordered array of IDs.
    // ----------------------------------------------------------
    public function reorder(int $workspaceId, array $orderedIds): void
    {
        foreach ($orderedIds as $position => $id) {
            $this->turso->execute(
                "UPDATE faq SET sort_order = ? WHERE id = ? AND workspace_id = ?",
                [$position, (int)$id, $workspaceId]
            );
        }
    }

    // ----------------------------------------------------------
    // Fuzzy search FAQ questions for a given workspace.
    //
    // We load all questions and score them using similar_text().
    // Returns items with score > threshold, sorted by score desc.
    // This is fast enough for hundreds of FAQ items.
    // ----------------------------------------------------------
    public function search(int $workspaceId, string $query, float $threshold = 30.0): array
    {
        $query = strtolower(trim($query));
        if ($query === '') return $this->list($workspaceId);

        $allItems = $this->list($workspaceId);
        $scored   = [];

        foreach ($allItems as $item) {
            $question = strtolower($item['question']);

            // Score 1: direct substring match (highest priority)
            if (str_contains($question, $query)) {
                $scored[] = array_merge($item, ['_score' => 100]);
                continue;
            }

            // Score 2: word-level match — check each word in query
            $queryWords    = explode(' ', $query);
            $matchedWords  = 0;
            foreach ($queryWords as $word) {
                if (strlen($word) > 2 && str_contains($question, $word)) {
                    $matchedWords++;
                }
            }
            if ($matchedWords > 0) {
                $wordScore = ($matchedWords / count($queryWords)) * 80;
                $scored[]  = array_merge($item, ['_score' => $wordScore]);
                continue;
            }

            // Score 3: character similarity using similar_text()
            similar_text($query, $question, $percent);
            if ($percent >= $threshold) {
                $scored[] = array_merge($item, ['_score' => $percent]);
            }
        }

        usort($scored, fn($a, $b) => $b['_score'] <=> $a['_score']);

        return $scored;
    }
}
