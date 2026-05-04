# RAG System — Complete Setup & Installation Guide

A full-stack Retrieval-Augmented Generation (RAG) system with an admin panel, embeddable chat widget, live admin chat, FAQ management, and analytics. Built in PHP with Turso as the vector database and Ollama for local AI.

---

## Table of Contents

1. [What This System Does](#what-this-system-does)
2. [Prerequisites](#prerequisites)
3. [Install Ollama](#install-ollama)
4. [Set Up Turso](#set-up-turso)
5. [Install PHP & Composer](#install-php--composer)
6. [Project Setup](#project-setup)
7. [Ingest Your Documents](#ingest-your-documents)
8. [Running the Project](#running-the-project)
9. [Features](#features)
10. [Admin Panel Guide](#admin-panel-guide)
11. [Widget Embed Guide](#widget-embed-guide)
12. [CLI Tools](#cli-tools)
13. [Multi-Language Implementations](#multi-language-implementations)
14. [Troubleshooting](#troubleshooting)

---

## What This System Does

```
Your Documents (PDF, DOCX, TXT)
        │
        ▼
Ingest Pipeline
  → Splits into chunks
  → Embeds each chunk into a vector (768 numbers)
  → Stores in Turso with native vector index
        │
        ▼
Chat Widget on your website
  → Visitor asks a question
  → Question embedded into vector
  → Turso finds most similar chunks
  → Local LLM generates answer from those chunks
  → Answer streamed back to visitor in real time
```

---

## Prerequisites

| Tool | Version | Required |
|---|---|---|
| PHP | 8.1+ | ✅ |
| Composer | Any | ✅ |
| Ollama | Latest | ✅ |
| Turso account | — | ✅ |
| Git | Any | Optional |

---

## Install Ollama

Ollama runs AI models locally on your machine. We use it for both generating text embeddings and chat responses.

### Windows
Download and run the installer from:
```
https://ollama.com/download/windows
```

### macOS
```bash
brew install ollama
```

### Linux
```bash
curl -fsSL https://ollama.com/install.sh | sh
```

### Pull the Required Models

After installing Ollama, open a terminal and pull the two models we need:

```bash
# Embedding model — converts text to vectors (274 MB)
ollama pull nomic-embed-text

# Chat model — generates answers (815 MB)
ollama pull gemma3:1b
```

> **Note:** `gemma3:1b` is the smallest model for testing. For better quality answers, use `gemma3:4b` (needs ~8GB RAM) or `qwen2.5:7b`.

### Verify Ollama is Running

```bash
# Check if Ollama is running
curl http://localhost:11434/api/tags

# Should return a JSON list of your installed models
```

If it's not running, start it:
```bash
ollama serve
```

---

## Set Up Turso

Turso is a cloud SQLite database with native vector search. It stores your document chunks and embeddings.

### Step 1 — Create a Turso Account

Go to [turso.tech](https://turso.tech) and sign up for a free account.

### Step 2 — Install the Turso CLI

**Windows (PowerShell):**
```powershell
winget install turso
```

**macOS/Linux:**
```bash
curl -sSfL https://get.tur.so/install.sh | bash
```

### Step 3 — Log In and Create a Database

```bash
# Log in
turso auth login

# Create a new database
turso db create my-rag-db

# Get the database URL
turso db show my-rag-db --url
# Output: libsql://my-rag-db-username.turso.io

# Create an auth token
turso db tokens create my-rag-db
# Output: eyJhbGci... (long token string)
```

### Step 4 — Save Your Credentials

You will need:
- **Database URL**: `https://my-rag-db-username.turso.io`
- **Auth Token**: `eyJhbGci...`

Keep these — you will put them in `config.php` in the next step.

> **Free tier:** Turso's free plan includes 500MB storage and 1 billion row reads/month — more than enough for most RAG applications.

---

## Install PHP & Composer

### PHP

**Windows:** Download from [windows.php.net](https://windows.php.net/download/) — get the Thread Safe x64 zip, extract, add to PATH.

**macOS:**
```bash
brew install php
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt install php8.2 php8.2-curl php8.2-mbstring php8.2-xml php8.2-zip
```

### Composer

Download from [getcomposer.org](https://getcomposer.org/download/) or:

**macOS/Linux:**
```bash
curl -sS https://getcomposer.org/installer | php
sudo mv composer.phar /usr/local/bin/composer
```

**Windows:** Download and run `Composer-Setup.exe` from getcomposer.org.

---

## Project Setup

### Step 1 — Clone or Download the Project

```bash
git clone <your-repo-url> rag-system
cd rag-system
```

Or simply download and extract the project folder.

### Step 2 — Install PHP Dependencies

```bash
cd public
composer install
```

This installs:
- `smalot/pdfparser` — PDF text extraction
- `phpoffice/phpword` — DOCX text extraction

### Step 3 — Configure the Project

Open `public/config.php` and fill in your credentials:

```php
return [
    // ---- Turso Database ----
    'turso_url'   => 'https://my-rag-db-username.turso.io',  // your DB URL
    'turso_token' => 'eyJhbGci...',                           // your auth token

    // ---- Ollama (Local AI) ----
    'ollama_url'      => 'http://127.0.0.1:11434',  // default Ollama URL
    'chat_model'      => 'gemma3:1b',                // chat model
    'embedding_model' => 'nomic-embed-text',         // embedding model
    'embedding_dims'  => 768,                        // must match model

    // ---- Ingestion Settings ----
    'chunk_size'    => 1500,   // characters per chunk
    'chunk_overlap' => 150,    // overlap between chunks
    'docs_dir'      => __DIR__ . '/docs',

    // ---- Retrieval ----
    'top_k' => 5,   // number of chunks to retrieve per query

    // ---- Security ----
    'encryption_key' => 'change-this-to-a-random-32-char-key!!',
];
```

### Step 4 — Create the Docs Folder

```bash
mkdir public/docs
```

Drop your `.txt`, `.pdf`, or `.docx` files in this folder.

---

## Ingest Your Documents

Before you can chat, you need to ingest your documents into the vector database.

### Add Documents

Copy your files into the `public/docs/` folder:
```
public/docs/
├── product-manual.pdf
├── faq-document.docx
└── company-info.txt
```

### Run the Ingest Script

```bash
cd public
php ingest.php
```

Expected output:
```
Ingesting (new file): product-manual.pdf
  -> 24 chunks
  Embedding chunk 1/24...
  Embedding chunk 2/24...
  ...
  Done: product-manual.pdf

New: 1 document(s)
```

### What Happens During Ingest

1. Each file is read and converted to plain text
2. Text is split into ~1500-character chunks with context headers
3. Each chunk is sent to Ollama's embedding model → returns 768 numbers
4. Chunk + embedding stored in Turso as a native `F32_BLOB` vector
5. DiskANN index built automatically for fast vector search

> **Re-ingesting:** If you update a file, just run `php ingest.php` again. It detects file changes via MD5 hash and only re-ingests changed files. Deleted files are automatically cleaned up.

---

## Running the Project

### Start the Server

```bash
cd rag-system
php -S localhost:8080 -t public
```

Then open your browser:

| URL | What it is |
|---|---|
| `http://localhost:8080` | Public website with widget |
| `http://localhost:8080/admin.html` | Admin panel |
| `http://localhost:8080/api/health.php` | API health check |

### First-Time Admin Setup

1. Go to `http://localhost:8080/admin.html`
2. Click **Sign Up** to create your admin account
3. Create a **Workspace** for your content
4. The workspace gives you a **Widget ID** for embedding the chat on any site

---

## Features

### 1. Document Ingestion
- Supports `.txt`, `.pdf`, `.docx` files
- Automatic chunking with section-aware splitting
- MD5 hash-based change detection — only re-ingests changed files
- Orphan cleanup — chunks deleted when source file is removed
- Real-time progress via SSE in admin panel

### 2. RAG Chat (AI Powered)
- Semantic vector search using Turso's native DiskANN index
- Query expansion for better retrieval recall
- Anti-hallucination system prompt with numbered sources
- Token-by-token streaming (typewriter effect)
- Full conversation history maintained per session

### 3. Admin Panel (`/admin.html`)
- **Dashboard** — workspace stats at a glance
- **Workspaces** — create isolated knowledge bases, each with its own widget
- **Documents** — upload files, track ingest status, delete
- **FAQ** — manual Q&A with rich text, fuzzy search, drag-to-reorder
- **FAQ Analytics** — view counts, unique visitors, 👍/👎 votes, satisfaction rate
- **Conversations** — full chat history per visitor with name/email/phone
- **Live Chat** — take over a visitor's session and chat in real time
- **Team Members** — invite team members, manage roles
- **Settings** — choose LLM model, embedding model, API keys per workspace

### 4. Chat Widget
Embed on any website with two lines of code:
```html
<script>
  window.RAGConfig = {
    widgetId: 'ws_your_widget_id',
    apiUrl:   'https://yourserver.com/api'
  };
</script>
<script src="https://yourserver.com/widget.js" defer></script>
```

Widget features:
- **Visitor capture form** — collects name (required), phone + email (optional)
- **FAQ tab** — searchable accordion with 👍/👎 voting
- **Chat tab** — AI-powered RAG chatbot with streaming responses
- **Session persistence** — chat history survives page refresh (localStorage)
- **Live support indicator** — 🟢 badge when admin takes over

### 5. Live Admin Chat
- Admin sees all active chat sessions in Conversations page
- Click **Take Over** to disable AI and chat directly with the visitor
- Visitor sees "🟢 Live Support" badge instantly
- Real-time 2-second polling — both sides see messages immediately
- Click **Release to AI** to hand back to the AI assistant

### 6. FAQ Analytics
- Every FAQ accordion open is tracked with IP and timestamp
- Visitors can vote 👍 or 👎 on each answer
- Admin sees: total views, unique visitors, up/down votes, satisfaction %
- Progress bar visualization per FAQ item

### 7. Session History
- All conversations saved to Turso permanently
- Admin can browse by workspace, see visitor name + contact info
- Click any conversation to view full message thread
- Color-coded bubbles: blue (visitor), gray (AI), green (admin)

---

## Admin Panel Guide

### Creating Your First Workspace

1. Log in to the admin panel at `/admin.html`
2. Go to **Workspaces** → click **+ New Workspace**
3. Give it a name (e.g. "Support Bot") and description
4. Click the workspace → go to **Documents** tab
5. Upload your files — they ingest automatically with progress shown
6. Go to **FAQ** tab → add manual Q&A items
7. Go to **Widget** tab → copy the embed code

### Managing Team Members

Only the account owner can manage team members:

1. Go to **Team Members** in the sidebar
2. Click **+ Invite Member**
3. Enter their name, email, and a temporary password
4. They log in and can manage workspaces

### Choosing AI Models

In each workspace → **Settings** tab:

| Model | Cost | Quality | Speed |
|---|---|---|---|
| 🟢 Local — gemma3:1b | Free | Basic | Fast |
| 🟢 Local — gemma3:4b | Free | Good | Medium |
| 🟡 OpenAI — gpt-4o-mini | ~$0.15/1M tokens | Great | Fast |
| 🔵 OpenAI — GPT-4o | ~$2.50/1M tokens | Excellent | Fast |
| 🔴 Voyage AI — voyage-3 | ~$0.06/1M tokens | Best for RAG | Fast |

For embedding models, changing requires re-ingesting all documents.

---

## Widget Embed Guide

### Basic Embed

Add to any HTML page before `</body>`:

```html
<script>
  window.RAGConfig = {
    widgetId: 'ws_your_widget_id_here',
    apiUrl:   'https://yourserver.com/api'
  };
</script>
<script src="https://yourserver.com/widget.js" defer></script>
```

### Finding Your Widget ID

Admin Panel → Workspaces → click your workspace → **Widget** tab → copy the Widget ID (looks like `ws_abc123def456`)

### Customization

The widget inherits your page's font stack. To customize colors, override CSS variables:

```html
<style>
  /* Change accent color */
  #rag-widget-btn { background: #your-color !important; }
</style>
```

---

## CLI Tools

These scripts run from the `public/` directory:

```bash
# Ingest all documents in docs/ folder
php ingest.php

# Debug: see what chunks get retrieved for a query
php query.php "your question here"

# One-shot RAG answer (no UI)
php rag.php "your question here"

# Interactive terminal chatbot
php chatbot.php
```

### CLI Chatbot Commands

Inside `php chatbot.php`:
- Type any question and press Enter to chat
- Type `clear` to reset conversation history
- Type `exit` or `quit` to stop

---

## Multi-Language Implementations

The same RAG pipeline is implemented in 15 languages in the `implementations/` folder:

| Language | Directory | Run |
|---|---|---|
| **PHP** (reference) | `implementations/php/` | `php chatbot.php` |
| **Python** | `implementations/python/` | `python chatbot.py` |
| **Go** | `implementations/go/` | `go run ./cmd/chatbot` |
| **Rust** | `implementations/rust/` | `cargo run --bin chatbot` |
| **TypeScript** | `implementations/typescript/` | `npm run chatbot` |
| **Spring Boot** | `implementations/springboot/` | `mvn spring-boot:run` |
| **C** | `implementations/c/` | `make && ./chatbot` |
| **C++** | `implementations/cpp/` | `cmake -B build && ./build/chatbot` |
| **Zig** | `implementations/zig/` | `zig build run-chatbot` |
| **Nim** | `implementations/nim/` | `nim c -r chatbot.nim` |
| **Odin** | `implementations/odin/` | `odin build chatbot/ && ./chatbot` |
| **V (Vlang)** | `implementations/v/` | `v run chatbot.v` |
| **Ada** | `implementations/ada/` | `gprbuild -P rag.gpr && ./bin/chatbot` |
| **Haskell** | `implementations/haskell/` | `stack exec chatbot` |
| **Erlang** | `implementations/erlang/` | `rebar3 escriptize && ./rag_chatbot` |

Each implementation has its own `README.md` with setup instructions.

---

## Troubleshooting

### "Widget not found"
- Check that `widget_id` in your embed code matches the Widget ID in the admin panel
- Make sure the admin server is running and accessible at the `apiUrl`

### "Ollama embed error"
- Run `ollama serve` to start Ollama
- Verify models are pulled: `ollama list`
- Check Ollama is on port 11434: `curl http://localhost:11434/api/tags`

### "Turso error (HTTP 401)"
- Your auth token has expired or is incorrect
- Generate a new token: `turso db tokens create your-db-name`
- Update `turso_token` in `config.php`

### Ingest runs but no chunks stored
- Check the Turso URL starts with `https://` not `libsql://`
- Verify your docs folder path in `config.php`
- Check file extensions are `.txt`, `.pdf`, or `.docx`

### Chat gives wrong answers
- Run `php query.php "your question"` to see what chunks are retrieved
- Check chunk quality — if chunks are too small, increase `chunk_size` in config
- Re-ingest after changing chunk settings: delete chunks from DB and run `php ingest.php`
- Try a larger model for better reasoning (e.g. `gemma3:4b`)

### Slow responses
- CPU-only Ollama takes 20-40 seconds per response — this is normal
- Switch to OpenAI API for 3-5 second responses (set in workspace settings)
- For GPU acceleration, install the CUDA version of Ollama

### Port already in use
```bash
# Use a different port
php -S localhost:9000 -t public
```

---

## Production Deployment

For production, replace the PHP built-in server with:

**Nginx + PHP-FPM:**
```nginx
server {
    listen 80;
    root /var/www/rag/public;
    index index.php;

    location / {
        try_files $uri $uri/ /index.php?$query_string;
    }

    location ~ \.php$ {
        fastcgi_pass unix:/var/run/php/php8.2-fpm.sock;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        include fastcgi_params;
    }
}
```

**For 100+ concurrent users:**
- Use OpenAI API instead of local Ollama (handles unlimited concurrent requests)
- Turso cloud handles concurrent connections fine
- PHP-FPM with 20+ workers handles concurrent web requests

---

## Architecture Overview

```
public/
├── index.php              Homepage template
├── admin.html             Admin panel (SPA)
├── widget.js              Embeddable chat widget
├── config.php             All configuration
├── ingest.php             CLI: ingest documents
├── query.php              CLI: debug retrieval
├── rag.php                CLI: one-shot RAG
├── chatbot.php            CLI: interactive chat
├── api/
│   ├── auth.php           Signup, login, sessions
│   ├── workspaces.php     Workspace CRUD + settings
│   ├── documents.php      File upload management
│   ├── ingest.php         SSE ingest progress
│   ├── faq.php            FAQ CRUD
│   ├── faq_analytics.php  View + vote tracking (public)
│   ├── faq_stats.php      Analytics overview (admin)
│   ├── sessions.php       Visitor session management
│   ├── conversations.php  Chat history (admin)
│   ├── widget_faq.php     Public FAQ API
│   ├── widget_chat.php    Public RAG chat SSE
│   ├── live_chat.php      Widget polling for admin messages
│   ├── admin_chat.php     Admin takeover + send
│   └── team.php           Team member management
├── src/
│   ├── Turso.php          Turso HTTP client
│   ├── Ollama.php         Ollama embed + stream
│   ├── DocumentParser.php txt/pdf/docx parser
│   ├── Chunker.php        Paragraph-aware chunker
│   ├── VectorSearch.php   Native Turso ANN search
│   └── Prompt.php         RAG system prompt builder
├── vendor/                Composer dependencies
├── storage/workspaces/    Uploaded files
└── docs/                  Documents to ingest
```
