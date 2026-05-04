-- chatbot.adb
-- Main procedure: interactive RAG chatbot.
--
-- Maintains a multi-turn conversation history and enriches each user turn
-- with context retrieved from the vector store.  The LLM sees:
--   [system]    -- RAG context injected fresh per turn
--   [user]      -- current question
--   [assistant] -- previous answer (for multi-turn coherence)
--   [user]      -- next question
--   ...
--
-- Ada design notes:
--   * Ada.Text_IO.Get_Line blocks until the user presses Enter; no threads
--     are needed for the input loop.
--   * The conversation history is a bounded array (Max_History turns).
--     WHY bounded: in a safety-critical system unbounded heap growth is
--     unacceptable.  We drop the oldest non-system messages when the buffer
--     fills, which is the standard sliding-window approach for LLM context.
--   * The loop exit condition ("quit" or "exit") is checked with a simple
--     string comparison; no regular-expression engine is needed.

with Ada.Text_IO;           use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Strings.Fixed;
with Ada.Characters.Handling;

with Rag_Config;
with Turso_Client;  use Turso_Client;
with Ollama_Client; use Ollama_Client;
with Vector_Math;

procedure Chatbot is

   Cfg    : constant Rag_Config.Config := Rag_Config.Default_Config;
   DB     : constant Turso_Client_Type := Turso_Client.Make_Client (Cfg);
   Ollama : constant Ollama_Client_Type := Ollama_Client.Make_Client (Cfg);

   -- Keep the last Max_History user+assistant turns (2 messages per turn).
   Max_History   : constant := 20;  -- 10 user + 10 assistant messages
   History       : Message_Array (1 .. Max_History);
   History_Count : Message_Index := 1;

   ---------------------------------------------------------------------------
   -- Retrieve the most-similar context chunks for a query.
   ---------------------------------------------------------------------------
   function Retrieve (Query_Text : String) return String is
      Query_Emb : constant Float_Array :=
        Ollama_Client.Embed (Ollama, Query_Text);

      Rows : constant Row_Array :=
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

      -- Insertion sort descending.
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

      declare
         Context : Unbounded_String;
         Top     : constant Natural := Integer'Min (Cfg.Top_K, Count);
      begin
         for I in 1 .. Top loop
            Append (Context, "--- context " & Integer'Image (I) & " ---" & ASCII.LF);
            Append (Context, Scored (I).Text);
            Append (Context, ASCII.LF & ASCII.LF);
         end loop;
         if Top = 0 then
            Append (Context, "(no relevant context found in the knowledge base)");
         end if;
         return To_String (Context);
      end;
   end Retrieve;

   ---------------------------------------------------------------------------
   -- Append_History adds a message to the history, dropping the oldest
   -- user/assistant pair when the buffer is full.
   ---------------------------------------------------------------------------
   procedure Append_History (Role : Message_Role; Content : String) is
   begin
      if History_Count > Max_History then
         -- Shift the array left by 2 (drop one user+assistant pair from
         -- the beginning, preserving relative order).
         for I in 1 .. Max_History - 2 loop
            History (I) := History (I + 2);
         end loop;
         History_Count := History_Count - 2;
      end if;
      History (History_Count) :=
        (Role    => Role,
         Content => To_Unbounded_String (Content));
      History_Count := History_Count + 1;
   end Append_History;

   ---------------------------------------------------------------------------
   -- Build_Messages assembles the full message array for this turn.
   -- The system message is always injected fresh with the latest context.
   ---------------------------------------------------------------------------
   function Build_Turn_Messages
     (Context : String; User_Input : String) return Message_Array
   is
      -- +2: room for the system message and the current user message.
      type Full_Array is array (1 .. Max_History + 2) of Message;
      Full  : Full_Array;
      Count : Message_Index := 1;

      System_Content : constant String :=
        "You are a helpful, concise assistant.  "
        & "Answer using the context below.  "
        & "If the context is insufficient, say ""I don't know""." & ASCII.LF
        & ASCII.LF & "Context:" & ASCII.LF & Context;
   begin
      -- System message always goes first.
      Full (Natural (Count)) :=
        (Role    => Role_System,
         Content => To_Unbounded_String (System_Content));
      Count := Count + 1;

      -- Previous history (user + assistant alternations).
      for I in 1 .. History_Count - 1 loop
         Full (Natural (Count)) := History (I);
         Count := Count + 1;
      end loop;

      -- Current user message.
      Full (Natural (Count)) :=
        (Role    => Role_User,
         Content => To_Unbounded_String (User_Input));
      Count := Count + 1;

      -- Convert to a properly sized Message_Array slice.
      declare
         Result : Message_Array (1 .. Count - 1);
      begin
         for I in Result'Range loop
            Result (I) := Full (Natural (I));
         end loop;
         return Result;
      end;
   end Build_Turn_Messages;

   ---------------------------------------------------------------------------
   -- Streaming printer: accumulates tokens and flushes to stdout.
   ---------------------------------------------------------------------------
   Response_Buffer : Unbounded_String;

   procedure Print_Token (Chunk : String) is
   begin
      Put (Chunk);
      Flush;
      Append (Response_Buffer, Chunk);
   end Print_Token;

   ---------------------------------------------------------------------------
   -- Trim helper: removes leading/trailing whitespace.
   ---------------------------------------------------------------------------
   function Trim (S : String) return String is
   begin
      return Ada.Strings.Fixed.Trim (S, Ada.Strings.Both);
   end Trim;

begin
   Put_Line ("=== Ada RAG Chatbot ===");
   Put_Line ("Type your question and press Enter.");
   Put_Line ("Type 'quit' or 'exit' to leave.");
   New_Line;

   -- Main interaction loop.
   loop
      Put ("You: ");
      Flush;

      declare
         Input     : String (1 .. 4096);
         Last      : Natural;
         User_Text : Unbounded_String;
      begin
         Get_Line (Input, Last);
         User_Text := To_Unbounded_String (Trim (Input (1 .. Last)));

         -- Exit condition.
         declare
            Lower : constant String :=
              Ada.Characters.Handling.To_Lower (To_String (User_Text));
         begin
            exit when Lower = "quit" or else Lower = "exit" or else
                      Lower = "q" or else Lower = "bye";
         end;

         if Length (User_Text) = 0 then
            -- Skip blank lines silently.
            goto Continue;
         end if;

         -- Retrieve context for this turn.
         declare
            Context   : constant String :=
              Retrieve (To_String (User_Text));
            Messages  : constant Message_Array :=
              Build_Turn_Messages (Context, To_String (User_Text));
         begin
            -- Clear the response buffer for this turn.
            Response_Buffer := Null_Unbounded_String;

            Put ("Assistant: ");
            Flush;

            Ollama_Client.Chat_Stream (Ollama, Messages, Print_Token'Access);
            New_Line;
            New_Line;

            -- Commit this turn to history.
            Append_History (Role_User,      To_String (User_Text));
            Append_History (Role_Assistant, To_String (Response_Buffer));
         end;
      end;

      <<Continue>>
      null;
   end loop;

   Put_Line ("Goodbye!");
end Chatbot;
