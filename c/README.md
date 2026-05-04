# C RAG Implementation

A complete Retrieval-Augmented Generation system written in C.

Connects to **Turso** (cloud libSQL vector store) and **Ollama** (local LLM inference).

---

## Prerequisites

### System packages (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev libssl-dev
```

### System packages (macOS with Homebrew)

```bash
brew install curl openssl
export CFLAGS="-I$(brew --prefix openssl)/include"
export LDFLAGS="-L$(brew --prefix openssl)/lib"
```

### Windows (MSYS2 / MinGW-w64)

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-curl mingw-w64-x86_64-openssl
```

### Ollama (local LLM server)

1. Download from https://ollama.com
2. Pull required models:

```bash
ollama pull nomic-embed-text
ollama pull gemma3:1b
```

Ollama must be running on `http://127.0.0.1:11434` before using any binary.

### cJSON

cJSON is **embedded directly** as `cJSON.c` and `cJSON.h`. No installation needed.

---

## Build

```bash
cd C:\Users\Engi\Ai\RAG\implementations\c

# Build all four binaries at once
make all

# Or build individually
make ingest
make query
make rag
make chatbot
```

---

## Usage

### 1. Ingest documents

Place `.txt` or `.md` files in `../../docs/` (i.e. `C:\Users\Engi\Ai\RAG\docs\`), then:

```bash
./ingest
```

This will:
- Create the `documents` table and vector index in Turso (if not already present)
- Read each document, split it into ~1500-character chunks
- Embed each chunk with `nomic-embed-text` via Ollama
- Store chunks and embeddings in Turso

Re-running `ingest` is safe — unchanged files (same MD5 hash) are skipped. Changed files are re-ingested.

### 2. Search only (no LLM)

```bash
./query "What is Your Knowledge Base?"
```

Prints the top-5 most relevant chunks with cosine distance scores.  
Useful for debugging retrieval quality before involving the LLM.

### 3. One-shot RAG answer

```bash
./rag "How do I send a check online?"
```

Embeds the question, retrieves context, builds a prompt, and streams the LLM's answer to stdout.

### 4. Interactive chatbot

```bash
./chatbot
```

Starts an interactive REPL with conversation history. Type `exit` or `quit` to stop.  
Context is refreshed from Turso on every turn so follow-up questions get relevant passages.

---

## Architecture

```
ingest.c ─────────────────────────────────────────────────────────────┐
  opendir/readdir → read_file → md5_hex → chunk_text → ollama_embed   │
  → turso_execute (INSERT)                                             │
                                                                       │
query.c / rag.c / chatbot.c ──────────────────────────────────────────┤
  ollama_embed → turso_fetch_all (vector_top_k) → ollama_chat_stream  │
                                                                       │
Shared modules:                                                        │
  config.h    — RagConfig struct, compiled-in defaults                 │
  http.c      — libcurl POST wrappers (buffered + streaming)           │
  json.c      — cJSON helpers + request builders                       │
  turso.c     — Turso HTTP pipeline API client                         │
  ollama.c    — Ollama /api/embed and /api/chat client                 │
  chunker.c   — Paragraph-aware text splitter                          │
  cJSON.c     — Embedded JSON parser (no external dependency)          │
```

---

## Configuration

All defaults are in `config.h`. Edit the `#define` constants and recompile:

| Constant | Default | Description |
|---|---|---|
| `DEFAULT_TURSO_URL` | Turso endpoint | Turso HTTP base URL |
| `DEFAULT_TURSO_TOKEN` | JWT token | Turso Bearer auth token |
| `DEFAULT_OLLAMA_URL` | `http://127.0.0.1:11434` | Ollama base URL |
| `DEFAULT_CHAT_MODEL` | `gemma3:1b` | LLM for answer generation |
| `DEFAULT_EMBEDDING_MODEL` | `nomic-embed-text` | Embedding model |
| `DEFAULT_EMBEDDING_DIMS` | `768` | Vector dimensions |
| `DEFAULT_TOP_K` | `5` | Chunks retrieved per query |
| `DEFAULT_CHUNK_SIZE` | `1500` | Max characters per chunk |

---

## Clean

```bash
make clean
```
