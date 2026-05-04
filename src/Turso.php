<?php

namespace RAG;

// ============================================================
// Turso — Our Database Connection
// ============================================================
// Turso is a cloud SQLite database. It does not have a PHP SDK,
// but it exposes a simple HTTP API that we call with curl.
//
// Every SQL query goes through two methods:
//   execute()  → run a query, don't care about rows returned
//   fetchAll() → run a query, get back an array of rows
// ============================================================

class Turso
{
    private string $url;
    private string $token;

    public function __construct(array $config)
    {
        $this->url   = rtrim($config['turso_url'], '/');
        $this->token = $config['turso_token'] ?? '';
    }

    // ----------------------------------------------------------
    // Run any SQL statement (INSERT, CREATE, DELETE, UPDATE).
    // Returns the raw result in case the caller needs it.
    // ----------------------------------------------------------
    public function execute(string $sql, array $params = []): array
    {
        $result = $this->sendToTurso($sql, $params);
        return $result['response']['result'] ?? [];
    }

    // ----------------------------------------------------------
    // Run a SELECT and return rows as a clean associative array.
    // Instead of getting back raw Turso column/value structures,
    // you get back plain PHP arrays like ['id' => 1, 'content' => '...']
    // ----------------------------------------------------------
    public function fetchAll(string $sql, array $params = []): array
    {
        $result = $this->sendToTurso($sql, $params);

        $columnNames = array_column($result['response']['result']['cols'] ?? [], 'name');
        $rawRows     = $result['response']['result']['rows'] ?? [];

        // Map each raw row from Turso's format into a simple key => value array
        return array_map(function ($rawRow) use ($columnNames) {
            $row = [];
            foreach ($columnNames as $index => $name) {
                $row[$name] = $rawRow[$index]['value'] ?? null;
            }
            return $row;
        }, $rawRows);
    }

    // ----------------------------------------------------------
    // The actual HTTP call to Turso's pipeline API.
    // Turso expects SQL as JSON over HTTP — we build that payload,
    // send it, and check for errors before returning.
    // ----------------------------------------------------------
    private function sendToTurso(string $sql, array $params): array
    {
        $statement = ['type' => 'execute', 'stmt' => ['sql' => $sql]];

        if (!empty($params)) {
            $statement['stmt']['args'] = array_map(
                fn($value) => $this->encodeValue($value),
                $params
            );
        }

        $payload = json_encode(['requests' => [$statement]]);

        $curl = curl_init("{$this->url}/v2/pipeline");
        curl_setopt_array($curl, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_POST           => true,
            CURLOPT_POSTFIELDS     => $payload,
            CURLOPT_HTTPHEADER     => $this->buildHeaders(),
        ]);

        $response = curl_exec($curl);
        $httpCode = curl_getinfo($curl, CURLINFO_HTTP_CODE);
        curl_close($curl);

        if ($response === false || $httpCode >= 400) {
            throw new \RuntimeException("Turso HTTP error ($httpCode): $response");
        }

        $data = json_decode($response, true);

        if (isset($data['results'][0]['error'])) {
            throw new \RuntimeException("Turso SQL error: " . $data['results'][0]['error']['message']);
        }

        return $data['results'][0] ?? [];
    }

    // ----------------------------------------------------------
    // Turso needs to know the type of each parameter value
    // (integer, float, text, or null) — we detect and tag it here.
    // ----------------------------------------------------------
    private function encodeValue(mixed $value): array
    {
        if (is_null($value))  return ['type' => 'null'];
        if (is_int($value))   return ['type' => 'integer', 'value' => (string) $value];
        if (is_float($value)) return ['type' => 'float',   'value' => (string) $value];
        return ['type' => 'text', 'value' => (string) $value];
    }

    // ----------------------------------------------------------
    // Every request to Turso needs a Content-Type header.
    // If a token is set (cloud mode), we also send Authorization.
    // ----------------------------------------------------------
    private function buildHeaders(): array
    {
        $headers = ['Content-Type: application/json'];

        if ($this->token !== '') {
            $headers[] = "Authorization: Bearer {$this->token}";
        }

        return $headers;
    }
}
