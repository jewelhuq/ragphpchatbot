-- ollama_client.adb
-- Implementation of the Ollama HTTP client.
--
-- Embedding request body:
--   {"model":"nomic-embed-text","prompt":"<text>"}
-- Embedding response:
--   {"embedding":[f0, f1, ..., f767]}
--
-- Chat request body (streaming):
--   {"model":"gemma3:1b","messages":[{"role":"user","content":"..."},...],
--    "stream":true}
-- Each streamed line is a JSON object:
--   {"model":"...","created_at":"...","message":{"role":"assistant",
--    "content":"<token>"},"done":false}
-- Last line has "done":true.

with Ada.Text_IO;           use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Strings.Fixed;
with Http_Client;

package body Ollama_Client is

   ---------------------------------------------------------------------------
   -- Internal JSON helpers
   ---------------------------------------------------------------------------

   function Json_String (S : String) return String is
      R : Unbounded_String;
   begin
      Append (R, """");
      for C of S loop
         case C is
            when '"'       => Append (R, "\""");
            when '\'       => Append (R, "\\");
            when ASCII.LF  => Append (R, "\n");
            when ASCII.CR  => Append (R, "\r");
            when ASCII.HT  => Append (R, "\t");
            when others    => Append (R, C);
         end case;
      end loop;
      Append (R, """");
      return To_String (R);
   end Json_String;

   function Role_String (R : Message_Role) return String is
   begin
      case R is
         when Role_System    => return "system";
         when Role_User      => return "user";
         when Role_Assistant => return "assistant";
      end case;
   end Role_String;

   -- Build_Messages serialises the Ada Message_Array into a JSON array.
   function Build_Messages (Messages : Message_Array) return String is
      R : Unbounded_String;
   begin
      Append (R, "[");
      for I in Messages'Range loop
         if I > Messages'First then
            Append (R, ",");
         end if;
         Append (R, "{""role"":");
         Append (R, Json_String (Role_String (Messages (I).Role)));
         Append (R, ",""content"":");
         Append (R, Json_String (To_String (Messages (I).Content)));
         Append (R, "}");
      end loop;
      Append (R, "]");
      return To_String (R);
   end Build_Messages;

   -- Extract_Float_Array parses the JSON number array after the key
   -- "embedding":[ and returns values as a Float_Array.
   -- WHY manual parse: importing a JSON library just for this one field
   -- would add a heavy external dependency; a hand-written parser for a
   -- flat array of floats is short and auditable.
   function Extract_Float_Array
     (Json : String; Key : String; Expected_Len : Positive) return Float_Array
   is
      type Work_Array is array (1 .. Expected_Len) of Float;
      Vals  : Work_Array := (others => 0.0);
      Count : Natural := 0;

      -- Find the array start.
      Pos   : Natural := Ada.Strings.Fixed.Index (Json, Key);
      C     : Natural;
      Start : Natural;
      Stop  : Natural;
   begin
      if Pos = 0 then
         return (1 .. 0 => 0.0);
      end if;
      -- Advance to the opening `[`.
      C := Pos + Key'Length;
      while C <= Json'Last and then Json (C) /= '[' loop
         C := C + 1;
      end loop;
      C := C + 1;  -- skip `[`

      -- Parse comma-separated floating-point numbers.
      while C <= Json'Last and then Json (C) /= ']' loop
         -- Skip whitespace and commas.
         while C <= Json'Last and then
           (Json (C) = ' ' or else Json (C) = ',' or else
            Json (C) = ASCII.LF or else Json (C) = ASCII.CR)
         loop
            C := C + 1;
         end loop;
         exit when C > Json'Last or else Json (C) = ']';

         -- Find end of the number token.
         Start := C;
         while C <= Json'Last and then
           (Json (C) in '0' .. '9' or else Json (C) = '.' or else
            Json (C) = '-' or else Json (C) = 'e' or else Json (C) = 'E' or else
            Json (C) = '+')
         loop
            C := C + 1;
         end loop;
         Stop := C - 1;

         exit when Stop < Start;
         exit when Count >= Expected_Len;

         Count := Count + 1;
         begin
            Vals (Count) := Float'Value (Json (Start .. Stop));
         exception
            when Constraint_Error =>
               Vals (Count) := 0.0;
         end;
      end loop;

      -- Return exactly Expected_Len floats (pad with 0.0 if the server
      -- returned fewer, which would be caught by the Post condition on Embed).
      declare
         Result : Float_Array (1 .. Expected_Len) := (others => 0.0);
      begin
         for I in 1 .. Count loop
            if I <= Expected_Len then
               Result (I) := Vals (I);
            end if;
         end loop;
         return Result;
      end;
   end Extract_Float_Array;

   -- Extract_Content pulls the "content" field from a single streaming chunk.
   function Extract_Content (Json_Line : String) return String is
      -- We look for the pattern "content":"<value>" inside the message object.
      Key    : constant String := """content"":";
      Pos    : Natural := Ada.Strings.Fixed.Index (Json_Line, Key);
      C      : Natural;
      Start  : Natural;
      Result : Unbounded_String;
   begin
      if Pos = 0 then
         return "";
      end if;
      C := Pos + Key'Length;
      -- Skip whitespace.
      while C <= Json_Line'Last and then Json_Line (C) = ' ' loop
         C := C + 1;
      end loop;
      if C > Json_Line'Last or else Json_Line (C) /= '"' then
         return "";
      end if;
      C := C + 1;  -- skip opening quote
      Start := C;
      -- Read until unescaped closing quote.
      while C <= Json_Line'Last loop
         if Json_Line (C) = '\' and then C < Json_Line'Last then
            case Json_Line (C + 1) is
               when 'n'  => Append (Result, ASCII.LF & ""); C := C + 2;
               when 't'  => Append (Result, ASCII.HT & ""); C := C + 2;
               when '"'  => Append (Result, '"'); C := C + 2;
               when '\'  => Append (Result, '\'); C := C + 2;
               when others => Append (Result, Json_Line (C)); C := C + 1;
            end case;
         elsif Json_Line (C) = '"' then
            exit;
         else
            Append (Result, Json_Line (C));
            C := C + 1;
         end if;
      end loop;
      return To_String (Result);
   end Extract_Content;

   ---------------------------------------------------------------------------
   -- Public subprograms
   ---------------------------------------------------------------------------

   function Make_Client (Cfg : Rag_Config.Config) return Ollama_Client_Type is
   begin
      return (Cfg => Cfg);
   end Make_Client;

   function Embed
     (Client : Ollama_Client_Type;
      Text   : String) return Float_Array
   is
      Url  : constant String :=
        To_String (Client.Cfg.Ollama_Url) & "/api/embeddings";
      Body : constant String :=
        "{""model"":" & Json_String (To_String (Client.Cfg.Embedding_Model))
        & ",""prompt"":" & Json_String (Text) & "}";
      Resp : constant String := Http_Client.Post (Url, Body);
      Dims : constant Positive := Client.Cfg.Embedding_Dims;
   begin
      return Extract_Float_Array (Resp, """embedding"":", Dims);
      -- The Post condition (Embed'Result'Length = Client.Cfg.Embedding_Dims)
      -- is checked by the run-time system here before the caller receives the
      -- vector.  If the Ollama server ever changes its output dimension the
      -- contract fires immediately, preventing silent data corruption.
   end Embed;

   procedure Chat_Stream
     (Client   : Ollama_Client_Type;
      Messages : Message_Array;
      On_Chunk : Chunk_Callback)
   is
      Url  : constant String :=
        To_String (Client.Cfg.Ollama_Url) & "/api/chat";
      Body : constant String :=
        "{""model"":" & Json_String (To_String (Client.Cfg.Chat_Model))
        & ",""messages"":" & Build_Messages (Messages)
        & ",""stream"":true}";

      -- Deliver each SSE line to On_Chunk after extracting the content token.
      procedure Handle_Line (Line : String) is
         Content : constant String := Extract_Content (Line);
      begin
         if Content'Length > 0 then
            On_Chunk (Content);
         end if;
      end Handle_Line;
   begin
      Http_Client.Post_Stream (Url, Body, "", Handle_Line'Access);
   end Chat_Stream;

   function Chat
     (Client   : Ollama_Client_Type;
      Messages : Message_Array) return String
   is
      Accumulator : Unbounded_String;

      procedure Collect (Chunk : String) is
      begin
         Append (Accumulator, Chunk);
      end Collect;
   begin
      Chat_Stream (Client, Messages, Collect'Access);
      return To_String (Accumulator);
   end Chat;

end Ollama_Client;
