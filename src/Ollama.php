<?php

namespace RAG;

// ============================================================
// Ollama — Our Local AI Brain
// ============================================================
// Ollama runs AI models on your own machine and exposes them
// via a simple HTTP API at localhost:11434.
//
// We use Ollama for two separate jobs:
//
//   1. embed()      — Turn text into a list of numbers (a vector)
//                     that captures its meaning. Used at ingest
//                     time and at query time.
//
//   2. chatStream() — Send a conversation to the LLM and stream
//                     the response token by token as it generates.
//                     This gives the "typing" effect in the UI.
// ============================================================

class Ollama
{
    private string $url;
    private string $chatModel;
    private string $embedModel;

    public function __construct(array $config)
    {
        $this->url        = rtrim($config['ollama_url'], '/');
        $this->chatModel  = $config['chat_model'];
        $this->embedModel = $config['embedding_model'];
    }

    // ----------------------------------------------------------
    // Turn a piece of text into a vector (list of floats).
    // Example: "check printing" → [0.23, -0.81, 0.44, ...]
    //
    // Similar texts produce similar vectors. This is how we find
    // relevant chunks — we compare the query vector to stored
    // chunk vectors and return the closest matches.
    // ----------------------------------------------------------
    public function embed(string $text): array
    {
        $response = $this->post('/api/embeddings', [
            'model'  => $this->embedModel,
            'prompt' => $text,
        ]);

        if (!isset($response['embedding'])) {
            throw new \RuntimeException("Ollama embedding failed. Response: " . json_encode($response));
        }

        return $response['embedding'];
    }

    // ----------------------------------------------------------
    // Send a conversation to the LLM and stream the response.
    //
    // Instead of waiting for the full answer (which can take 30s+
    // on CPU), we call $onChunk() with each token as it arrives.
    // The caller decides what to do with each token — print it,
    // send it via SSE to the browser, etc.
    //
    // $messages format:
    //   [
    //     ['role' => 'system',    'content' => 'You are...'],
    //     ['role' => 'user',      'content' => 'Question?'],
    //     ['role' => 'assistant', 'content' => 'Answer.'],
    //     ['role' => 'user',      'content' => 'Follow-up?'],
    //   ]
    // ----------------------------------------------------------
    public function chatStream(array $messages, callable $onChunk): void
    {
        $curl = curl_init("{$this->url}/api/chat");
        curl_setopt_array($curl, [
            CURLOPT_POST          => true,
            CURLOPT_POSTFIELDS    => json_encode([
                'model'    => $this->chatModel,
                'messages' => $messages,
                'stream'   => true,
            ]),
            CURLOPT_HTTPHEADER    => ['Content-Type: application/json'],
            CURLOPT_TIMEOUT       => 120,

            // Ollama streams JSON objects separated by newlines.
            // This callback fires each time a chunk of data arrives,
            // we parse each line and hand the token to the caller.
            CURLOPT_WRITEFUNCTION => function ($curl, $data) use ($onChunk) {
                foreach (explode("\n", $data) as $line) {
                    $line = trim($line);
                    if ($line === '') continue;

                    $json = json_decode($line, true);
                    if (isset($json['message']['content'])) {
                        $onChunk($json['message']['content']);
                    }
                }
                return strlen($data); // curl requires we return byte count
            },
        ]);

        curl_exec($curl);
        curl_close($curl);
    }

    // ----------------------------------------------------------
    // Generic helper: POST JSON to any Ollama endpoint and
    // return the decoded response. Used internally by embed().
    // ----------------------------------------------------------
    private function post(string $endpoint, array $body): array
    {
        $curl = curl_init("{$this->url}{$endpoint}");
        curl_setopt_array($curl, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_POST           => true,
            CURLOPT_POSTFIELDS     => json_encode($body),
            CURLOPT_HTTPHEADER     => ['Content-Type: application/json'],
            CURLOPT_TIMEOUT        => 60,
        ]);

        $response = curl_exec($curl);
        curl_close($curl);

        return json_decode($response, true) ?? [];
    }
}
