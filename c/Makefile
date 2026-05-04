# Makefile — C RAG system build
#
# Targets:
#   all      — build ingest, query, rag, chatbot
#   ingest   — document ingestion pipeline
#   query    — vector search CLI
#   rag      — one-shot RAG answer
#   chatbot  — interactive chatbot REPL
#   clean    — remove compiled objects and binaries
#
# Dependencies:
#   libcurl  — HTTP client (sudo apt install libcurl4-openssl-dev)
#   openssl  — MD5 hashing in ingest (sudo apt install libssl-dev)
#   cJSON    — embedded directly as cJSON.c / cJSON.h (no install needed)
#   libm     — math functions used by cJSON (usually included by default)
#
# The -pthread flag is included defensively; libcurl may need it depending
# on the build configuration on the host system.
#
# On macOS: OpenSSL may be in $(brew --prefix openssl). Export:
#   export CFLAGS="-I$(brew --prefix openssl)/include"
#   export LDFLAGS="-L$(brew --prefix openssl)/lib"
# then run make.

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
          -Wno-deprecated-declarations $(EXTRA_CFLAGS)
LDFLAGS = -lcurl -lssl -lcrypto -lm -pthread $(EXTRA_LDFLAGS)

# Source files shared by all binaries
COMMON_SRCS = cJSON.c http.c json.c turso.c ollama.c chunker.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

# All binaries
BINARIES = ingest query rag chatbot

.PHONY: all clean

all: $(BINARIES)

# ---------------------------------------------------------------
# Object file compilation
# Each .c → .o with dependency on all headers (simpler than proper
# dependency tracking for a project this size).
# ---------------------------------------------------------------

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------
# Binary targets
# ---------------------------------------------------------------

ingest: ingest.o $(COMMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

query: query.o $(COMMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

rag: rag.o $(COMMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

chatbot: chatbot.o $(COMMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

# ---------------------------------------------------------------
# Explicit header dependencies
# Ensures that changing a header triggers recompilation of all
# source files that include it.
# ---------------------------------------------------------------

cJSON.o:   cJSON.c cJSON.h
http.o:    http.c http.h
json.o:    json.c json.h cJSON.h
turso.o:   turso.c turso.h http.h json.h config.h cJSON.h
ollama.o:  ollama.c ollama.h http.h json.h config.h cJSON.h
chunker.o: chunker.c chunker.h config.h
ingest.o:  ingest.c config.h turso.h ollama.h chunker.h
query.o:   query.c config.h ollama.h turso.h
rag.o:     rag.c config.h ollama.h turso.h
chatbot.o: chatbot.c config.h ollama.h turso.h

# ---------------------------------------------------------------
# Clean
# ---------------------------------------------------------------

clean:
	rm -f $(COMMON_OBJS) ingest.o query.o rag.o chatbot.o
	rm -f $(BINARIES)
	rm -f ingest.exe query.exe rag.exe chatbot.exe
