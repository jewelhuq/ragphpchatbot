-- ingest.adb
-- Main procedure: scans the docs/ directory, chunks each file,
-- generates embeddings, and stores them in Turso.
--
-- This is a batch job; it is meant to be run once (or re-run when documents
-- change).  It is NOT the interactive loop -- see chatbot.adb for that.
--
-- Ada design notes:
--   * We use Ada.Directories to iterate over the docs directory; this is
--     portable across POSIX and Windows without #ifdef guards.
--   * Every string operation uses Unbounded_String to avoid fixed-buffer
--     constraints.  The compiler enforces this: String arithmetic on
--     stack-allocated arrays would require knowing the length at compile time.

with Ada.Text_IO;           use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Directories;
with Ada.Command_Line;

with Rag_Config;
with Turso_Client;  use Turso_Client;
with Ollama_Client; use Ollama_Client;
with Chunker;       use Chunker;
with Vector_Math;

procedure Ingest is

   Cfg    : constant Rag_Config.Config := Rag_Config.Default_Config;
   DB     : constant Turso_Client_Type := Turso_Client.Make_Client (Cfg);
   Ollama : constant Ollama_Client_Type := Ollama_Client.Make_Client (Cfg);

   Docs_Dir : constant String := To_String (Cfg.Docs_Dir);

   ---------------------------------------------------------------------------
   -- Ensure_Table creates the embeddings table if it does not already exist.
   -- WHY IF NOT EXISTS: makes Ingest idempotent; running it twice should not
   -- fail or duplicate the schema.
   ---------------------------------------------------------------------------
   procedure Ensure_Table is
   begin
      Put_Line ("Creating table if needed...");
      Turso_Client.Execute
        (DB,
         "CREATE TABLE IF NOT EXISTS embeddings (" &
         "  id        INTEGER PRIMARY KEY AUTOINCREMENT," &
         "  source    TEXT    NOT NULL," &
         "  chunk     TEXT    NOT NULL," &
         "  embedding TEXT    NOT NULL" &
         ")",
         Empty_Params);
      Put_Line ("Table ready.");
   end Ensure_Table;

   ---------------------------------------------------------------------------
   -- Read_File slurps a text file into an Unbounded_String.
   ---------------------------------------------------------------------------
   function Read_File (Path : String) return String is
      F      : File_Type;
      Result : Unbounded_String := Null_Unbounded_String;
      Line   : String (1 .. 8192);
      Last   : Natural;
   begin
      Open (F, In_File, Path);
      while not End_Of_File (F) loop
         Get_Line (F, Line, Last);
         Append (Result, Line (1 .. Last));
         Append (Result, ASCII.LF & "");
      end loop;
      Close (F);
      return To_String (Result);
   exception
      when others =>
         if Is_Open (F) then Close (F); end if;
         return "";
   end Read_File;

   ---------------------------------------------------------------------------
   -- Process_File chunks and ingests a single document.
   ---------------------------------------------------------------------------
   procedure Process_File (Path : String) is
      Content : constant String := Read_File (Path);
      Chunks  : String_Array (1 .. Max_Chunks);
      N       : Chunk_Index;
   begin
      if Content'Length = 0 then
         Put_Line ("  Skipping empty file: " & Path);
         return;
      end if;

      Put_Line ("  Processing: " & Path);

      declare
         Result_Chunks : constant String_Array :=
           Chunker.Chunk (Content, Path, Cfg.Chunk_Size);
      begin
         N := Result_Chunks'Last;
         Chunks (1 .. N) := Result_Chunks;
      end;

      Put_Line ("    " & Chunk_Index'Image (N) & " chunk(s)");

      for I in 1 .. N loop
         declare
            Chunk_Text : constant String := To_String (Chunks (I));
            Embedding  : constant Float_Array :=
              Ollama_Client.Embed (Ollama, Chunk_Text);
            Emb_Json   : constant String :=
              Vector_Math.Float_To_Json (Embedding);
         begin
            Turso_Client.Execute
              (DB,
               "INSERT INTO embeddings (source, chunk, embedding) " &
               "VALUES (?, ?, ?)",
               (1 => Text_Param (Path),
                2 => Text_Param (Chunk_Text),
                3 => Text_Param (Emb_Json)));
            Put (".");
         end;
      end loop;
      New_Line;
   end Process_File;

   ---------------------------------------------------------------------------
   -- Ingest_Directory walks the docs directory recursively.
   ---------------------------------------------------------------------------
   procedure Ingest_Directory (Dir : String) is
      use Ada.Directories;
      Search  : Search_Type;
      Entry_V : Directory_Entry_Type;
   begin
      if not Exists (Dir) then
         Put_Line ("Warning: docs directory not found: " & Dir);
         Put_Line ("Creating it with a sample file...");
         Ada.Directories.Create_Directory (Dir);
         declare
            F : File_Type;
         begin
            Create (F, Out_File, Dir & "/sample.txt");
            Put_Line (F, "Ada is the world's safest programming language.");
            Put_Line (F, "It was designed by Jean Ichbiah in 1977 for the US DoD.");
            Put_Line (F, "Ada features strong typing, design-by-contract, and no null pointers.");
            Put_Line (F, "SPARK Ada adds formal verification for safety-critical systems.");
            Put_Line (F, "Ada is used in aviation (Boeing 777), military, and space systems.");
            Close (F);
         end;
      end if;

      Start_Search (Search, Dir, "",
                    (Ordinary_File => True, others => False));
      while More_Entries (Search) loop
         Get_Next_Entry (Search, Entry_V);
         declare
            Full : constant String := Full_Name (Entry_V);
         begin
            Process_File (Full);
         end;
      end loop;
      End_Search (Search);
   end Ingest_Directory;

begin
   Put_Line ("=== Ada RAG Ingest ===");
   Put_Line ("Docs directory : " & Docs_Dir);
   Put_Line ("Turso URL      : " & To_String (Cfg.Turso_Url));
   Put_Line ("Embedding model: " & To_String (Cfg.Embedding_Model));
   New_Line;

   Ensure_Table;
   Ingest_Directory (Docs_Dir);

   Put_Line ("Ingest complete.");
end Ingest;
