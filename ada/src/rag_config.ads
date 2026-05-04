-- rag_config.ads
-- Package specification for RAG system configuration.
--
-- WHY this package exists: Ada demands that every configurable value live in
-- exactly one place.  Scattering magic strings through the code base would
-- violate the Single-Responsibility Principle and make audits (required in
-- safety-critical domains) painful.  We centralise everything here.
--
-- Ada safety note: all string fields are Unbounded_String so the record never
-- carries a dangling pointer and cannot overflow a fixed buffer.

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;

package Rag_Config is

   -- The single configuration record for the entire RAG pipeline.
   -- Every component receives a copy of (or reference to) this record;
   -- no global mutable state is ever needed.
   type Config is record
      -- Turso (libsql) database endpoint
      Turso_Url   : Unbounded_String;
      -- Bearer token for Turso HTTP API authentication
      Turso_Token : Unbounded_String;

      -- Ollama inference server base URL
      Ollama_Url  : Unbounded_String;
      -- Name of the chat-completion model served by Ollama
      Chat_Model  : Unbounded_String;
      -- Name of the embedding model; must produce Embedding_Dims floats
      Embedding_Model : Unbounded_String;
      -- Dimension of the embedding vectors (must match the model)
      Embedding_Dims  : Positive;

      -- Number of nearest-neighbour chunks to retrieve per query
      Top_K    : Positive;
      -- Directory that Ingest will scan for plain-text documents
      Docs_Dir : Unbounded_String;
      -- Maximum number of characters per text chunk
      Chunk_Size : Positive;
   end record;

   -- Default_Config returns a fully populated Config ready for use.
   -- WHY a function rather than a constant: Ada elaboration rules guarantee
   -- that Unbounded_String values initialised inside a function body are
   -- properly allocated, whereas a package-level constant would require a
   -- more complex initialisation expression.
   function Default_Config return Config;

end Rag_Config;
