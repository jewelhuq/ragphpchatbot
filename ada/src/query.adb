-- query.adb
-- Main procedure: takes a query string from the command line, retrieves the
-- most similar chunks from Turso, and prints them ranked by cosine similarity.
--
-- Usage:
--   ./bin/query "What is Ada design by contract?"
--
-- Ada design notes:
--   * Ada.Command_Line provides the argument without shell word-splitting
--     hazards; no string injection is possible.
--   * We sort by descending similarity score using a simple insertion sort;
--     for Top_K <= 10 this is perfectly adequate and avoids a container
--     dependency.

with Ada.Text_IO;           use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Command_Line;

with Rag_Config;
with Turso_Client;  use Turso_Client;
with Ollama_Client; use Ollama_Client;
with Vector_Math;

procedure Query is

   Cfg    : constant Rag_Config.Config := Rag_Config.Default_Config;
   DB     : constant Turso_Client_Type := Turso_Client.Make_Client (Cfg);
   Ollama : constant Ollama_Client_Type := Ollama_Client.Make_Client (Cfg);

   ---------------------------------------------------------------------------
   -- A search result: similarity score + the chunk text.
   ---------------------------------------------------------------------------
   type Result is record
      Score : Float;
      Chunk : Unbounded_String;
      Source: Unbounded_String;
   end record;

   Max_Results : constant := 1024;
   type Result_Array is array (1 .. Max_Results) of Result;

   ---------------------------------------------------------------------------
   -- Search retrieves candidate chunks from the database and ranks them.
   ---------------------------------------------------------------------------
   function Search (Query_Text : String) return Result_Array is
      Query_Emb : constant Float_Array :=
        Ollama_Client.Embed (Ollama, Query_Text);

      -- Fetch all stored embeddings.  In a production system you would use an
      -- ANN index; for this reference implementation a full table scan is
      -- acceptable for small document collections.
      Rows : constant Row_Array :=
        Turso_Client.Fetch_All
          (DB, "SELECT source, chunk, embedding FROM embeddings", Empty_Params);

      Results : Result_Array;
      Count   : Natural := 0;
   begin
      for R in Rows'Range loop
         declare
            Source_Val : constant String :=
              To_String (Rows (R)(1));
            Chunk_Val  : constant String :=
              To_String (Rows (R)(2));
            Emb_Json   : constant String :=
              To_String (Rows (R)(3));
            Row_Emb    : constant Float_Array :=
              Vector_Math.Json_To_Float (Emb_Json, Cfg.Embedding_Dims);
         begin
            if Row_Emb'Length = Query_Emb'Length then
               Count := Count + 1;
               exit when Count > Max_Results;
               Results (Count) :=
                 (Score  => Vector_Math.Cosine_Similarity (Query_Emb, Row_Emb),
                  Chunk  => To_Unbounded_String (Chunk_Val),
                  Source => To_Unbounded_String (Source_Val));
            end if;
         end;
      end loop;

      -- Insertion sort by descending score.
      -- WHY insertion sort: Count is bounded by Max_Results (1024) and
      -- Top_K is typically 5; the actual useful portion is tiny.
      for I in 2 .. Count loop
         declare
            Key : constant Result := Results (I);
            J   : Integer := I - 1;
         begin
            while J >= 1 and then Results (J).Score < Key.Score loop
               Results (J + 1) := Results (J);
               J := J - 1;
            end loop;
            Results (J + 1) := Key;
         end;
      end loop;

      return Results;
   end Search;

begin
   if Ada.Command_Line.Argument_Count = 0 then
      Put_Line ("Usage: query <question>");
      return;
   end if;

   declare
      Query_Text : constant String := Ada.Command_Line.Argument (1);
      Results    : constant Result_Array := Search (Query_Text);
      Top        : constant Natural :=
        Integer'Min (Cfg.Top_K, Results'Last);
   begin
      Put_Line ("Query: " & Query_Text);
      New_Line;
      Put_Line ("Top " & Integer'Image (Top) & " results:");
      Put_Line (String'(1 .. 60 => '-'));

      for I in 1 .. Top loop
         Put_Line ("Rank  :" & Integer'Image (I));
         Put_Line ("Score :" & Float'Image (Results (I).Score));
         Put_Line ("Source:" & To_String (Results (I).Source));
         Put_Line ("Chunk :");
         Put_Line (To_String (Results (I).Chunk));
         Put_Line (String'(1 .. 60 => '-'));
      end loop;
   end;
end Query;
