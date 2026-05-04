-- ollama_client.ads
-- Specification for the Ollama inference client.
--
-- Ollama exposes two endpoints we use:
--   POST /api/embeddings  -- returns a float vector
--   POST /api/chat        -- streams chat tokens (Server-Sent Events)
--
-- Ada safety notes:
--   * The Post condition on Embed is a machine-checked contract that the
--     returned vector has exactly 768 dimensions.  In a SPARK mode this
--     would be a provable theorem; at run time it becomes an assertion that
--     fires before the caller ever sees a malformed vector.
--   * Float_Array uses Positive indices, so there is no zero-index trap.
--   * Message_Array uses a bounded discriminant; callers cannot overflow it.

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Rag_Config;

package Ollama_Client is

   -- Float_Array is the type used for embedding vectors.
   -- Using Positive range <> means the compiler rejects any attempt to create
   -- an array with a zero or negative length discriminant.
   type Float_Array is array (Positive range <>) of Float;

   -- Role of a chat message: the model differentiates between system
   -- instructions, user turns, and model (assistant) responses.
   type Message_Role is (Role_System, Role_User, Role_Assistant);

   -- A single chat message.
   type Message is record
      Role    : Message_Role;
      Content : Unbounded_String;
   end record;

   Max_Messages : constant := 128;
   type Message_Index is range 1 .. Max_Messages;
   type Message_Array is array (Message_Index range <>) of Message;

   -- Chunk_Callback is the type for the streaming token handler.
   -- WHY access-to-procedure: avoids tagged-type overhead while still
   -- allowing closure-style usage through GNAT's access-to-subprogram
   -- attributes (via Unrestricted_Access where needed).
   type Chunk_Callback is access procedure (Chunk : String);

   -- The Ollama client handle.
   type Ollama_Client_Type is record
      Cfg : Rag_Config.Config;
   end record;

   function Make_Client (Cfg : Rag_Config.Config) return Ollama_Client_Type;

   -- Embed converts a text string into a 768-dimensional embedding vector.
   --
   -- Pre WHY: an empty string produces a meaningless embedding; catching this
   -- here prevents a silent garbage vector from polluting the vector store.
   --
   -- Post WHY: if the Ollama server returns a vector of the wrong dimension
   -- (e.g. because a different model is configured), this contract fires
   -- immediately rather than letting a wrong-sized vector silently corrupt
   -- cosine-similarity calculations later in the pipeline.
   function Embed
     (Client : Ollama_Client_Type;
      Text   : String) return Float_Array
   with Pre  => Text'Length > 0,
        Post => Embed'Result'Length = Client.Cfg.Embedding_Dims;

   -- Chat_Stream sends a multi-turn conversation to the chat model and
   -- delivers each streaming token chunk to On_Chunk.
   --
   -- Pre WHY: a conversation with no messages is semantically invalid;
   -- this prevents an HTTP round-trip that would only return an error.
   -- On_Chunk /= null is required to avoid a null-pointer dereference.
   procedure Chat_Stream
     (Client   : Ollama_Client_Type;
      Messages : Message_Array;
      On_Chunk : Chunk_Callback)
   with Pre => Messages'Length > 0 and then On_Chunk /= null;

   -- Chat is a non-streaming convenience wrapper that accumulates all
   -- chunks and returns the complete response.
   function Chat
     (Client   : Ollama_Client_Type;
      Messages : Message_Array) return String
   with Pre => Messages'Length > 0;

end Ollama_Client;
