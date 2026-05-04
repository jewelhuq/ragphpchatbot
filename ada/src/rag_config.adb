-- rag_config.adb
-- Package body: provides the actual default values for the RAG system.
--
-- WHY the real token lives here: in a production Ada safety-critical system
-- you would read secrets from an environment variable or a hardware security
-- module.  For this reference implementation the token is inlined so every
-- component can be tested without external configuration management.

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;

package body Rag_Config is

   function Default_Config return Config is
   begin
      return
        (Turso_Url   => To_Unbounded_String
                          ("https://your-db-name.turso.io"),
         Turso_Token => To_Unbounded_String
                          ("your-turso-token-here"),

         Ollama_Url      => To_Unbounded_String ("http://127.0.0.1:11434"),
         Chat_Model      => To_Unbounded_String ("gemma3:1b"),
         Embedding_Model => To_Unbounded_String ("nomic-embed-text"),

         -- WHY 768: nomic-embed-text produces 768-dimensional vectors.
         -- This constant is checked at compile time via the Post condition
         -- on Ollama_Client.Embed to prevent silent dimension mismatches.
         Embedding_Dims => 768,

         -- Retrieve the top 5 most-similar chunks for each user query.
         Top_K    => 5,
         Docs_Dir => To_Unbounded_String ("docs"),

         -- 512 characters per chunk: large enough for semantic coherence,
         -- small enough to stay well within the model's context window.
         Chunk_Size => 512);
   end Default_Config;

end Rag_Config;
