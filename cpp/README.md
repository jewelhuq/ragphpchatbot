# C++ RAG — Retrieval-Augmented Generation

A self-contained C++20 implementation of a RAG pipeline backed by
[Turso](https://turso.tech) (libSQL over HTTP) for vector storage and
[Ollama](https://ollama.com) for local embeddings and generation.

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | >= 3.20 | Build system |
| C++ compiler | C++20 capable | GCC 11+, Clang 14+, or MSVC 2022 |
| libcurl | any recent | Headers + import library required |
| OpenSSL | 1.1 or 3.x | Headers + libraries for MD5 |
| Ollama | running locally | `ollama serve` on `127.0.0.1:11434` |
| nomic-embed-text | pulled in Ollama | `ollama pull nomic-embed-text` |
| gemma3:1b | pulled in Ollama | `ollama pull gemma3:1b` |

> **nlohmann/json** is fetched automatically by CMake at configure time — no
> manual installation needed.

### Installing dependencies

**Ubuntu / Debian**
```bash
sudo apt install build-essential cmake libcurl4-openssl-dev libssl-dev
```

**macOS (Homebrew)**
```bash
brew install cmake curl openssl
```

**Windows (vcpkg)**
```powershell
vcpkg install curl openssl
# then pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake to cmake
```

---

## Build

```bash
# Configure (Release build by default)
cmake -B build

# With vcpkg on Windows:
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

# Compile all four executables
cmake --build build --config Release
```

Outputs (Linux/macOS): `build/ingest`, `build/query`, `build/rag`, `build/chatbot`  
Outputs (Windows): `build\Release\ingest.exe`, etc.

---

## Usage

### 1. Start Ollama

```bash
ollama serve          # if not already running as a service
ollama pull nomic-embed-text
ollama pull gemma3:1b
```

### 2. Add documents

Place `.txt` or `.md` files in the `docs/` directory (created relative to
where you run the binary):

```bash
mkdir docs
echo "Our return policy allows returns within 30 days of purchase." > docs/policy.txt
```

### 3. Ingest documents

```bash
./build/ingest
```

The ingest tool:
- Creates the `documents` table and vector index in Turso (idempotent).
- Scans `docs/` recursively for `.txt` and `.md` files.
- Computes an MD5 hash of each file; skips files whose hash has not changed.
- Splits changed files into overlapping chunks (512 chars, 64-char overlap).
- Embeds each chunk with `nomic-embed-text` (768 dimensions).
- Inserts chunk text + embedding into Turso.

Re-run whenever you add or modify documents. Only changed files are
re-processed.

### 4. Search (debug)

```bash
./build/query "What is the return policy?"
```

Embeds the query, runs `vector_top_k` against Turso, and prints the top-5
matching chunks with source paths and content previews. Use this to verify
that retrieval is working before trying the full RAG pipeline.

### 5. Single-shot RAG answer

```bash
./build/rag "Explain the return policy in simple terms."
```

Retrieves the top-5 chunks, builds a context-grounded prompt, and streams
the model's answer to stdout.

### 6. Interactive chatbot

```bash
./build/chatbot
```

Starts an interactive session. Each turn:
1. Embeds your message.
2. Retrieves fresh context from Turso.
3. Passes the updated context + full conversation history to `gemma3:1b`.
4. Streams the reply token-by-token.

Type `exit` or `quit` (or press Ctrl-D) to end the session. History is
capped at the last 20 turns (40 messages) to avoid exceeding the model's
context window.

---

## Configuration

All settings live in `include/config.hpp` in `RagConfig::defaults()`.
Override individual fields in code, or extend the struct to read from
environment variables.

| Field | Default | Description |
|-------|---------|-------------|
| `tursoUrl` | `https://your-db-name.turso.io` | Turso HTTP endpoint |
| `tursoToken` | (JWT) | Turso bearer token |
| `tableName` | `documents` | Table name in Turso |
| `ollamaUrl` | `http://127.0.0.1:11434` | Ollama daemon URL |
| `embedModel` | `nomic-embed-text` | Embedding model |
| `chatModel` | `gemma3:1b` | Chat/generation model |
| `embeddingDims` | `768` | Vector dimensions |
| `chunkSize` | `512` | Target chunk size (chars) |
| `chunkOverlap` | `64` | Overlap between chunks (chars) |
| `topK` | `5` | Nearest neighbours to retrieve |
| `docsDir` | `docs` | Directory scanned by ingest |

---

## Architecture

```
docs/               <- Source documents (txt, md)
    |
    v
[ingest]            <- DocumentParser -> Chunker -> OllamaClient.embed()
    |                                                     |
    v                                                     v
[TursoClient]  <--  INSERT (content, embedding F32_BLOB)
    |
    v  (vector_top_k)
[query / rag / chatbot]  ->  OllamaClient.chatStream()  ->  stdout
```

### File map

| File | Role |
|------|------|
| `include/config.hpp` | Central configuration struct |
| `include/http.hpp` / `src/http.cpp` | libcurl RAII wrapper |
| `include/turso.hpp` / `src/turso.cpp` | Turso HTTP API client |
| `include/ollama.hpp` / `src/ollama.cpp` | Ollama embed + chat client |
| `include/chunker.hpp` / `src/chunker.cpp` | Paragraph-aware text chunker |
| `include/parser.hpp` / `src/parser.cpp` | Document reader (.txt/.md) |
| `src/ingest.cpp` | Ingest pipeline main |
| `src/query.cpp` | Vector search debug tool main |
| `src/rag.cpp` | Single-shot RAG main |
| `src/chatbot.cpp` | Interactive chatbot main |
| `CMakeLists.txt` | Build definition |

---

## Adding PDF/DOCX Support

The `DocumentParser` stubs raise `std::runtime_error` with instructions
when it encounters a `.pdf` or `.docx` file.

**PDF**: link `libpoppler-cpp` and implement `parsePdf()` in `src/parser.cpp`.  
**DOCX**: unzip the file (it is a ZIP archive) and parse `word/document.xml`
with a SAX or DOM XML library, then implement `parseDocx()`.

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| `curl_global_init failed` | libcurl not found or wrong version |
| `Turso SQL error: ...` | Check token, URL, or SQL syntax |
| `Ollama embed error` | Model not pulled; `ollama pull nomic-embed-text` |
| Empty results from query | Run `ingest` first; check `docsDir` path |
| CMake can't find CURL | Pass `-DCURL_ROOT=...` or use vcpkg toolchain |
