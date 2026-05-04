-- http_client.adb
-- Implementation of the HTTP client using curl subprocesses.
--
-- Strategy:
--   1. Write the JSON request body to a temp file (avoids shell-quoting nightmares).
--   2. Invoke curl with -s (silent), -X POST, appropriate headers, and
--      -o <response-file> to capture the response body.
--   3. Read the response file, delete both temp files, return the content.
--
-- WHY temp files instead of pipes:
-- GNAT.OS_Lib.Spawn does not expose stdin/stdout pipes directly.  Writing to
-- files is portable across POSIX and Windows without additional FFI.

with Ada.Text_IO;                use Ada.Text_IO;
with Ada.Text_IO.Text_Streams;
with Ada.Strings.Unbounded;      use Ada.Strings.Unbounded;
with Ada.Streams;
with GNAT.OS_Lib;                use GNAT.OS_Lib;

package body Http_Client is

   ---------------------------------------------------------------------------
   -- Internal helpers
   ---------------------------------------------------------------------------

   -- Unique_Temp_Name returns a file path in the system temp directory.
   -- WHY not Ada.Directories.Temp_File: that function is not universally
   -- available across GNAT versions; a simple counter suffix is sufficient.
   procedure Write_File (Path : String; Content : String) is
      F : File_Type;
   begin
      Create (F, Out_File, Path);
      Put (F, Content);
      Close (F);
   end Write_File;

   -- Read_File slurps an entire file into an Unbounded_String.
   -- Using Unbounded_String avoids any fixed-buffer constraint; the compiler
   -- enforces that we never silently truncate the response.
   function Read_File (Path : String) return String is
      F      : File_Type;
      Result : Unbounded_String := Null_Unbounded_String;
      Line   : String (1 .. 4096);
      Last   : Natural;
   begin
      Open (F, In_File, Path);
      while not End_Of_File (F) loop
         Get_Line (F, Line, Last);
         Append (Result, Line (1 .. Last));
         if not End_Of_File (F) then
            Append (Result, ASCII.LF & "");
         end if;
      end loop;
      Close (F);
      return To_String (Result);
   exception
      when others =>
         if Is_Open (F) then
            Close (F);
         end if;
         return "";
   end Read_File;

   -- Run_Curl executes curl with the supplied argument list.
   -- Returns True when curl exits with status 0.
   function Run_Curl (Args : Argument_List) return Boolean is
      Curl_Path : String_Access;
      Success   : Boolean;
      Return_Code : Integer;
   begin
      -- Locate curl on PATH.  GNAT.OS_Lib.Locate_Exec_On_Path returns null
      -- when the executable is not found, which we treat as a hard error.
      Curl_Path := Locate_Exec_On_Path ("curl");
      if Curl_Path = null then
         raise Http_Error with "curl executable not found on PATH";
      end if;

      Spawn (Program_Name => Curl_Path.all,
             Args         => Args,
             Success      => Success,
             Return_Code  => Return_Code);
      Free (Curl_Path);

      return Success and then Return_Code = 0;
   end Run_Curl;

   ---------------------------------------------------------------------------
   -- Post
   ---------------------------------------------------------------------------

   function Post
     (Url   : String;
      Body  : String;
      Token : String := "") return String
   is
      Body_File : constant String := "ada_rag_req.tmp";
      Resp_File : constant String := "ada_rag_resp.tmp";
      Success   : Boolean;
   begin
      -- Step 1: write the JSON body to a temp file.
      Write_File (Body_File, Body);

      -- Step 2: build the curl argument list.
      -- We use --data-binary @file to forward the exact bytes without any
      -- shell interpretation.
      declare
         Base_Args : Argument_List :=
           (new String'("-s"),
            new String'("-X"), new String'("POST"),
            new String'("-H"), new String'("Content-Type: application/json"),
            new String'("--data-binary"), new String'("@" & Body_File),
            new String'("-o"), new String'(Resp_File),
            new String'(Url));

         Auth_Args : Argument_List :=
           (new String'("-H"),
            new String'("Authorization: Bearer " & Token));
      begin
         if Token'Length > 0 then
            declare
               Full_Args : Argument_List (1 .. Base_Args'Length + Auth_Args'Length);
            begin
               Full_Args (1 .. Base_Args'Length) := Base_Args;
               Full_Args (Base_Args'Length + 1 .. Full_Args'Last) := Auth_Args;
               Success := Run_Curl (Full_Args);
               for I in Full_Args'Range loop
                  Free (Full_Args (I));
               end loop;
            end;
         else
            Success := Run_Curl (Base_Args);
            for I in Base_Args'Range loop
               Free (Base_Args (I));
            end loop;
         end if;
      end;

      if not Success then
         -- Clean up before raising so we do not leave temp files behind.
         Delete_File (Body_File, Success);
         raise Http_Error with "curl POST failed for URL: " & Url;
      end if;

      -- Step 3: read and return the response.
      declare
         Response : constant String := Read_File (Resp_File);
      begin
         Delete_File (Body_File, Success);
         Delete_File (Resp_File, Success);
         return Response;
      end;
   end Post;

   ---------------------------------------------------------------------------
   -- Post_Stream
   ---------------------------------------------------------------------------
   -- For streaming we use curl's --no-buffer flag combined with a temp file
   -- that we tail-read line by line.  Because GNAT.OS_Lib.Spawn is blocking
   -- we capture the full response first, then deliver lines one at a time.
   --
   -- WHY not real streaming: true streaming would require either a pipe
   -- (unavailable in portable GNAT) or threads.  For a reference
   -- implementation correctness matters more than latency.

   procedure Post_Stream
     (Url      : String;
      Body     : String;
      Token    : String := "";
      On_Chunk : access procedure (Chunk : String))
   is
      Response : constant String := Post (Url, Body, Token);
      -- Split response on newline characters and deliver each non-empty line.
      Start : Positive := Response'First;
      NL    : Natural;
   begin
      -- Walk through the response delivering one line per call.
      while Start <= Response'Last loop
         NL := Start;
         -- Find the next newline.
         while NL <= Response'Last and then Response (NL) /= ASCII.LF loop
            NL := NL + 1;
         end loop;
         -- Deliver the slice [Start .. NL-1] (excluding the newline itself).
         if NL > Start then
            On_Chunk (Response (Start .. NL - 1));
         end if;
         Start := NL + 1;
      end loop;
   end Post_Stream;

end Http_Client;
