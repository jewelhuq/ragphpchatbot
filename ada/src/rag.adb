-- rag.adb
-- Main procedure: full RAG pipeline in a single pass.
--
-- Given a user question (from the command line) this program:
--   1. Embeds the question with nomic-embed-text.
--   2. Retrieves the Top_K most similar chunks from Turso.
--   3. Assembles a prompt that includes the retrieved context.
--   4. Streams the LLM answer token-by-token to stdout.
--
-- This is the "one-shot" mode: ask one question and exit.
-- For an interactive loop see chatbot.adb.
--
-- Ada design notes:
--   * The context prompt is assembled as an Unbounded_String; no fixed buffer
--     could safely accommodate a variable number of retrieved chunks.
--   * The streaming callback is a named nested procedure whose 'Access is
--     passed to Chat_Stream; Ada's access-to-subprogram rules guarantee
--     the procedure remains valid for the duration of the call.

with Ada.Text_IO;           use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Command_Line;

with Rag_Config;
with Turso_Client;  use Turso_Client;
with Ollama_Client; use Ollama_Client;
with Vector_Math;

procedure Rag is

   Cfg    : constant Rag_Config.Config := Rag_Config.Default_Config;
   DB     : constant Turso_Client_Type := Turso_Client.Make_Client (Cfg);
   Ollama : constant Ollama_Client_Type := Ollama_Client.Make_Client (Cfg);

   ---------------------------------------------------------------------------
   -- Retrieve the Top_K chunks most similar to the query embedding.
   ---------------------------------------------------------------------------
   function Retrieve (Query_Emb : Float_Array) return String is
      Rows    : constant Row_Array :=
        Turso_Client.Fetch_All
          (DB, "SELECT chunk, embedding FROM embeddings", Empty_Params);

      type Scored_Chunk is record
         Score : Float;
         Text  : Unbounded_String;
      end record;
      type Scored_Array is array (1 .. 1024) of Scored_Chunk;

      Scored : Scored_Array;
      Count  : Natural := 0;
   begin
      for R in Rows'Range loop
         declare
            Chunk_Text : constant String := To_String (Rows (R)(1));
            Emb_Json   : constant String := To_String (Rows (R)(2));
            Row_Emb    : constant Float_Array :=
              Vector_Math.Json_To_Float (Emb_Json, Cfg.Embedding_Dims);
         begin
            if Row_Emb'Length = Query_Emb'Length then
               Count := Count + 1;
               exit when Count > 1024;
               Scored (Count) :=
                 (Score => Vector_Math.Cosine_Similarity (Query_Emb, Row_Emb),
                  Text  => To_Unbounded_String (Chunk_Text));
            end if;
         end;
      end loop;

      -- Sort descending by score (insertion sort).
      for I in 2 .. Count loop
         declare
            Key : constant Scored_Chunk := Scored (I);
            J   : Integer := I - 1;
         begin
            while J >= 1 and then Scored (J).Score < Key.Score loop
               Scored (J + 1) := Scored (J);
               J := J - 1;
            end loop;
            Scored (J + 1) := Key;
         end;
      end loop;

      -- Concatenate the Top_K chunks into a single context string.
      declare
         Context : Unbounded_String;
         Top     : constant Natural := Integer'Min (Cfg.Top_K, Count);
      begin
         for I in 1 .. Top loop
            Append (Context, "--- chunk ");
            Append (Context, Integer'Image (I));
            Append (Context, " ---" & ASCII.LF);
            Append (Context, Scored (I).Text);
            Append (Context, ASCII.LF & ASCII.LF);
         end loop;
         return To_String (Context);
      end;
   end Retrieve;

   ---------------------------------------------------------------------------
   -- Build_System_Prompt constructs the system message that grounds the LLM
   -- in the retrieved context.
   ---------------------------------------------------------------------------
   function Build_System_Prompt (Context : String) return String is
   begin
      return "You are a helpful assistant.  Answer the user's question using "
        & "ONLY the context provided below.  If the context does not contain "
        & "enough information say ""I don't know"" rather than guessing." & ASCII.LF
        & ASCII.LF
        & "Context:" & ASCII.LF
        & Context;
   end Build_System_Prompt;

   -- Token printer: writes each streaming token directly to stdout without
   -- a newline so the response appears character-by-character.
   procedure Print_Token (Chunk : String) is
   begin
      Put (Chunk);
      -- Flush after each token so the user sees output immediately.
      Flush;
   end Print_Token;

begin
   if Ada.Command_Line.Argument_Count = 0 then
      Put_Line ("Usage: rag <question>");
      return;
   end if;

   declare
      Question  : constant String := Ada.Command_Line.Argument (1);
      Query_Emb : constant Float_Array :=
        Ollama_Client.Embed (Ollama, Question);
      Context   : constant String := Retrieve (Query_Emb);
      Sys_Prompt: constant String := Build_System_Prompt (Context);

      Messages  : constant Message_Array :=
        (1 => (Role    => Role_System,
               Content => To_Unbounded_String (Sys_Prompt)),
         2 => (Role    => Role_User,
               Content => To_Unbounded_String (Question)));
   begin
      Put_Line ("Question: " & Question);
      New_Line;
      Put ("Answer: ");

      Ollama_Client.Chat_Stream (Ollama, Messages, Print_Token'Access);
      New_Line;
   end;
end Rag;
