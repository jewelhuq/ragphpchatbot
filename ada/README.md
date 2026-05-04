# Ada RAG Implementation

A Retrieval-Augmented Generation (RAG) system written in Ada — the world's
safest programming language — connecting to Turso (libSQL) for vector storage
and Ollama for local inference.

---

## Why Ada?

Ada was designed in 1977 by Jean Ichbiah under contract to the US Department
of Defense specifically to eliminate the class of bugs that cause catastrophic
failures in safety-critical systems.  Every language feature was chosen to
make incorrect programs *uncompilable or detectable at run time*.

| Ada safety feature | What it prevents |
|---|---|
| Strong static typing | Passing a `Float` where an `Integer` is expected; mixing incompatible units |
| No implicit null pointers | Null-pointer dereferences that crash C/C++ programs |
| No buffer overflows | Fixed-size array accesses are range-checked; `Unbounded_String` grows safely |
| Design by contract (`Pre`/`Post`) | Calling a function with invalid arguments; returning a malformed result |
| `Positive` subtype | Accidentally using zero or a negative number as an array index |
| Elaboration order control | Accessing a global variable before it is initialised |
| SPARK subset + formal verification | Mathematical proof that no run-time error can occur (optional, not required here) |

In this RAG system, contracts appear on every safety-critical operation:

```ada
-- The embedding vector MUST have exactly 768 dimensions.
-- If Ollama changes its model, this fires BEFORE the wrong vector
-- reaches the database, preventing silent data corruption.
function Embed (Client : Ollama_Client_Type; Text : String)
  return Float_Array
with Pre  => Text'Length > 0,
     Post => Embed'Result'Length = Client.Cfg.Embedding_Dims;

-- Cosine similarity is undefined for vectors of different dimensions.
-- The Pre condition makes the requirement machine-checked.
function Cosine_Similarity (A, B : Float_Array) return Float
with Pre  => A'Length = B'Length and then A'Length > 0,
     Post => Cosine_Similarity'Result >= -1.0
             and then Cosine_Similarity'Result <= 1.0;
```

---

## Prerequisites

### Required

- **GNAT compiler** — one of:
  - [FSF GNAT](https://gcc.gnu.org/wiki/GNAT) (part of GCC, `sudo apt install gnat` on Debian/Ubuntu)
  - [GNAT Community Edition](https://www.adacore.com/download) (free, includes GPS IDE and gprbuild)
  - [Alire](https://alire.ada.dev/) — the Ada package manager (`alr build` works too)
- **gprbuild** — GNAT project builder (included with GNAT Community; `sudo apt install gprbuild` on Debian/Ubuntu)
- **curl** — the HTTP client used by `Http_Client` (`curl` must be on `PATH`)
- **Ollama** — running locally on `http://127.0.0.1:11434`
  - Pull models: `ollama pull nomic-embed-text && ollama pull gemma3:1b`
- **Turso account** with the database configured in `src/rag_config.adb`

### Checking your toolchain

```sh
gnat --version      # should print GCC/GNAT version
gprbuild --version  # should print gprbuild version
curl --version      # should print curl version
ollama list         # should show nomic-embed-text and gemma3:1b
```

---

## Building

### With gprbuild (recommended)

```sh
cd implementations/ada
gprbuild -P rag.gpr
```

Executables are placed in `bin/`:

```
bin/ingest
bin/query
bin/rag
bin/chatbot
```

### With gnatmake (alternative)

```sh
cd implementations/ada
mkdir -p obj bin

# Compile and link each main unit manually.
gnatmake -Isrc -D obj -o bin/ingest  src/ingest.adb  -gnat2012 -gnata
gnatmake -Isrc -D obj -o bin/query   src/query.adb   -gnat2012 -gnata
gnatmake -Isrc -D obj -o bin/rag     src/rag.adb     -gnat2012 -gnata
gnatmake -Isrc -D obj -o bin/chatbot src/chatbot.adb -gnat2012 -gnata
```

### Windows (PowerShell)

```powershell
cd implementations\ada
gprbuild -P rag.gpr
```

On Windows the executables will be named `ingest.exe`, `query.exe`, etc.

---

## Running

### 1. Ingest documents

Place plain-text (`.txt`) files in the `docs/` directory, then run:

```sh
./bin/ingest
```

The ingest program will:
1. Create the `embeddings` table in Turso (idempotent).
2. Scan every file in `docs/`.
3. Split each file into overlapping 512-character chunks (25% overlap).
4. Embed each chunk with `nomic-embed-text` via Ollama.
5. Store the chunk text and embedding JSON in Turso.

If `docs/` does not exist, Ingest creates it with a sample Ada fact file.

### 2. Query (retrieval only, no LLM answer)

```sh
./bin/query "What is Ada design by contract?"
```

Prints the top-5 most similar chunks with their cosine similarity scores.

### 3. One-shot RAG answer

```sh
./bin/rag "How does Ada prevent buffer overflows?"
```

Retrieves context and streams a grounded LLM answer to stdout.

### 4. Interactive chatbot

```sh
./bin/chatbot
```

Enter questions at the `You:` prompt.  The chatbot maintains a conversation
history of up to 10 turns.  Type `quit` or `exit` to leave.

---

## Project structure

```
implementations/ada/
├── rag.gpr              GNAT project file (build configuration)
├── src/
│   ├── rag_config.ads   Configuration record specification
│   ├── rag_config.adb   Default values (URLs, tokens, model names)
│   ├── http_client.ads  HTTP client specification (curl subprocess)
│   ├── http_client.adb  HTTP client body
│   ├── turso_client.ads Turso DB client specification
│   ├── turso_client.adb Turso DB client body (JSON builder + parser)
│   ├── ollama_client.ads Ollama client specification (embed + chat)
│   ├── ollama_client.adb Ollama client body
│   ├── chunker.ads      Text chunker specification
│   ├── chunker.adb      Sliding-window chunker body
│   ├── vector_math.ads  Cosine similarity + JSON serialisation spec
│   ├── vector_math.adb  Vector math body
│   ├── ingest.adb       Main: batch document ingestion
│   ├── query.adb        Main: retrieval-only query
│   ├── rag.adb          Main: one-shot RAG answer
│   └── chatbot.adb      Main: interactive multi-turn chatbot
├── docs/                Place .txt documents here before running ingest
├── obj/                 Compiled object files (created by gprbuild)
└── bin/                 Executable files (created by gprbuild)
```

---

## Ada package conventions

Every Ada package consists of two files:

| Extension | Purpose |
|---|---|
| `.ads` | **Spec** (specification) — the public API visible to callers |
| `.adb` | **Body** (implementation) — the private implementation |

This separation enforces information hiding at the language level: callers can
only see what is declared in the `.ads` file.  The compiler enforces this;
there is no way to access a private implementation detail from outside the
package.

---

## Configuration

Edit `src/rag_config.adb` to change:

| Field | Default | Description |
|---|---|---|
| `Turso_Url` | `https://your-db-name.turso.io` | Turso database endpoint |
| `Turso_Token` | (see file) | Bearer token for Turso auth |
| `Ollama_Url` | `http://127.0.0.1:11434` | Ollama server address |
| `Chat_Model` | `gemma3:1b` | Chat completion model |
| `Embedding_Model` | `nomic-embed-text` | Embedding model |
| `Embedding_Dims` | `768` | Vector dimensions (must match model) |
| `Top_K` | `5` | Chunks retrieved per query |
| `Docs_Dir` | `docs` | Directory scanned by ingest |
| `Chunk_Size` | `512` | Characters per chunk |

After changing `Embedding_Dims` you must re-run `ingest` to regenerate all
embeddings, because the Post condition on `Embed` will reject vectors of the
old dimension.

---

## SPARK formal verification (advanced)

The Pre/Post contracts in this code are Ada 2012 contracts, enforced at run
time.  To upgrade to compile-time formal proofs:

1. Install [SPARK Pro or SPARK Community](https://www.adacore.com/sparkpro).
2. Add `with SPARK_Mode => On;` to each package spec.
3. Run `gnatprove -P rag.gpr --level=2`.
4. GNATprove will attempt to discharge every Pre/Post condition as a
   mathematical theorem.  Any unproved condition is flagged as a warning or
   error with a precise explanation.

This is how Ada is used in DO-178C (aviation), EN 50128 (railways), and
MIL-STD-882 (military) certified software.
