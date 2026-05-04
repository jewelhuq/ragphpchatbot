-- turso_client.adb
-- Implementation of the Turso HTTP API client.
--
-- The Turso pipeline endpoint accepts:
--   POST /v2/pipeline
--   Body: {"requests": [{"type":"execute","stmt":{"sql":"...","args":[...]}}]}
--
-- We build JSON strings manually rather than pulling in an external library.
-- WHY manual JSON: Ada's ecosystem lacks a universally available JSON package.
-- Manual building is tedious but safe: we control every byte and can prove
-- (by inspection) that the output is well-formed.
--
-- JSON parsing follows a similar strategy: we extract values by searching
-- for known keys.  This is brittle for general JSON but robust enough for
-- the fixed schema that Turso returns.

with Ada.Text_IO;           use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Strings.Fixed;
with Http_Client;

package body Turso_Client is

   ---------------------------------------------------------------------------
   -- Internal JSON helpers
   ---------------------------------------------------------------------------

   -- Json_String wraps a plain string in JSON double quotes and escapes the
   -- characters that JSON requires to be escaped.
   function Json_String (S : String) return String is
      Result : Unbounded_String;
   begin
      Append (Result, """");
      for C of S loop
         case C is
            when '"'  => Append (Result, "\""");
            when '\'  => Append (Result, "\\");
            when ASCII.LF => Append (Result, "\n");
            when ASCII.CR => Append (Result, "\r");
            when ASCII.HT => Append (Result, "\t");
            when others => Append (Result, C);
         end case;
      end loop;
      Append (Result, """");
      return To_String (Result);
   end Json_String;

   -- Build_Params converts an Ada Param_Array into the JSON array that Turso
   -- expects for positional arguments.
   function Build_Params (Params : Param_Array) return String is
      Result : Unbounded_String;
   begin
      Append (Result, "[");
      for I in Params'Range loop
         if I > Params'First then
            Append (Result, ",");
         end if;
         case Params (I).Kind is
            when Param_Text =>
               Append (Result, "{""type"":""text"",""value"":");
               Append (Result, Json_String (To_String (Params (I).Value)));
               Append (Result, "}");
            when Param_Null =>
               Append (Result, "{""type"":""null""}");
         end case;
      end loop;
      Append (Result, "]");
      return To_String (Result);
   end Build_Params;

   -- Build_Request constructs the full pipeline request body.
   function Build_Request (Sql : String; Params : Param_Array) return String is
   begin
      return "{""requests"":[{""type"":""execute"",""stmt"":{""sql"":"
        & Json_String (Sql)
        & ",""args"":"
        & Build_Params (Params)
        & "}}]}";
   end Build_Request;

   ---------------------------------------------------------------------------
   -- Minimal JSON extraction helpers
   -- These work by scanning for the key pattern and extracting the value.
   -- They are NOT a general JSON parser; they are sufficient for Turso's
   -- fixed-schema responses.
   ---------------------------------------------------------------------------

   -- Find_After locates the first occurrence of Key in Source and returns
   -- the substring that starts immediately after it.  Returns "" on miss.
   function Find_After (Source, Key : String) return String is
      Pos : Natural;
   begin
      Pos := Ada.Strings.Fixed.Index (Source, Key);
      if Pos = 0 then
         return "";
      end if;
      return Source (Pos + Key'Length .. Source'Last);
   end Find_After;

   -- Extract_String_Value pulls the first JSON string value after a given
   -- key.  E.g. given `"value":"hello"` it returns `hello`.
   function Extract_String_Value (Fragment : String) return String is
      Start, Stop : Natural;
   begin
      -- Find the opening quote of the value.
      Start := Ada.Strings.Fixed.Index (Fragment, """");
      if Start = 0 then
         return "";
      end if;
      Start := Start + 1;  -- skip the opening quote
      Stop  := Start;
      while Stop <= Fragment'Last loop
         if Fragment (Stop) = '"' and then
           (Stop = Fragment'First or else Fragment (Stop - 1) /= '\')
         then
            return Fragment (Start .. Stop - 1);
         end if;
         Stop := Stop + 1;
      end loop;
      return "";
   end Extract_String_Value;

   -- Parse_Rows is the main response parser.  It scans the Turso JSON for
   -- rows/cells and builds the Row_Array.  It handles the compact format:
   --   {"results":[{"type":"ok","response":{"type":"execute","result":
   --     {"cols":[...],"rows":[[{"type":"text","value":"..."},...],...],...}}}]}
   function Parse_Rows (Json : String) return Row_Array is
      Rows_Out : Row_Array (1 .. Max_Rows);
      Row_Count : Row_Index := 1;
      -- We scan for the pattern `"rows":[[` and then parse cell arrays.
      Cursor : Natural;
      Tail   : Natural;
   begin
      -- Locate the rows array.
      Cursor := Ada.Strings.Fixed.Index (Json, """rows"":");
      if Cursor = 0 then
         return Empty_Rows;
      end if;
      -- Advance past `"rows":[`
      Cursor := Cursor + 8;
      -- Skip whitespace and the opening bracket of the rows array.
      while Cursor <= Json'Last and then Json (Cursor) /= '[' loop
         Cursor := Cursor + 1;
      end loop;
      if Cursor > Json'Last then
         return Empty_Rows;
      end if;
      Cursor := Cursor + 1;  -- skip `[`

      -- Each row is itself a JSON array: `[cell, cell, ...]`.
      Row_Loop : loop
         -- Skip whitespace.
         while Cursor <= Json'Last and then
           (Json (Cursor) = ' ' or else Json (Cursor) = ASCII.LF or else
            Json (Cursor) = ASCII.CR or else Json (Cursor) = ASCII.HT)
         loop
            Cursor := Cursor + 1;
         end loop;

         exit Row_Loop when Cursor > Json'Last or else Json (Cursor) /= '[';
         exit Row_Loop when Row_Count > Max_Rows;

         -- Parse cells within this row.
         declare
            Col : Col_Index := 1;
         begin
            Cursor := Cursor + 1;  -- skip `[`
            Cell_Loop : loop
               -- Skip whitespace.
               while Cursor <= Json'Last and then
                 (Json (Cursor) = ' ' or else Json (Cursor) = ASCII.LF or else
                  Json (Cursor) = ASCII.CR or else Json (Cursor) = ASCII.HT)
               loop
                  Cursor := Cursor + 1;
               end loop;

               exit Cell_Loop when Cursor > Json'Last or else Json (Cursor) = ']';

               -- Each cell is `{"type":"text","value":"..."}` or `{"type":"null"}`.
               -- Find the closing `}`.
               Tail := Cursor;
               declare
                  Depth : Natural := 0;
               begin
                  while Tail <= Json'Last loop
                     if Json (Tail) = '{' then
                        Depth := Depth + 1;
                     elsif Json (Tail) = '}' then
                        Depth := Depth - 1;
                        exit when Depth = 0;
                     end if;
                     Tail := Tail + 1;
                  end loop;
               end;

               -- Extract the cell's value field.
               declare
                  Cell_Json : constant String := Json (Cursor .. Tail);
                  Val_Frag  : constant String :=
                    Find_After (Cell_Json, """value"":");
                  Cell_Val  : constant String :=
                    (if Val_Frag'Length > 0
                     then Extract_String_Value (Val_Frag)
                     else "");
               begin
                  if Col <= Max_Cols then
                     Rows_Out (Row_Count)(Col) :=
                       To_Unbounded_String (Cell_Val);
                     Col := Col + 1;
                  end if;
               end;

               Cursor := Tail + 1;
               -- Skip optional comma between cells.
               while Cursor <= Json'Last and then Json (Cursor) = ',' loop
                  Cursor := Cursor + 1;
               end loop;
            end loop Cell_Loop;

            Cursor := Cursor + 1;  -- skip `]` closing the row
         end;

         -- Advance Row_Count.
         Row_Count := Row_Count + 1;

         -- Skip optional comma between rows.
         while Cursor <= Json'Last and then
           (Json (Cursor) = ',' or else Json (Cursor) = ' ' or else
            Json (Cursor) = ASCII.LF)
         loop
            Cursor := Cursor + 1;
         end loop;
      end loop Row_Loop;

      if Row_Count = 1 then
         return Empty_Rows;
      end if;
      return Rows_Out (1 .. Row_Count - 1);
   end Parse_Rows;

   ---------------------------------------------------------------------------
   -- Public subprograms
   ---------------------------------------------------------------------------

   function Make_Client (Cfg : Rag_Config.Config) return Turso_Client_Type is
   begin
      return (Cfg => Cfg);
   end Make_Client;

   function Text_Param (Value : String) return Param is
   begin
      return (Kind => Param_Text, Value => To_Unbounded_String (Value));
   end Text_Param;

   function Null_Param return Param is
   begin
      return (Kind => Param_Null, Value => Null_Unbounded_String);
   end Null_Param;

   procedure Execute
     (Client : Turso_Client_Type;
      Sql    : String;
      Params : Param_Array := Empty_Params)
   is
      Url      : constant String :=
        To_String (Client.Cfg.Turso_Url) & "/v2/pipeline";
      Token    : constant String := To_String (Client.Cfg.Turso_Token);
      Body     : constant String := Build_Request (Sql, Params);
      Response : constant String := Http_Client.Post (Url, Body, Token);
   begin
      -- Check the response for an error marker.
      if Ada.Strings.Fixed.Index (Response, """type"":""error""") > 0 then
         Put_Line ("Turso error response: " & Response);
         raise Constraint_Error with "Turso execute error";
      end if;
   end Execute;

   function Fetch_All
     (Client : Turso_Client_Type;
      Sql    : String;
      Params : Param_Array := Empty_Params) return Row_Array
   is
      Url      : constant String :=
        To_String (Client.Cfg.Turso_Url) & "/v2/pipeline";
      Token    : constant String := To_String (Client.Cfg.Turso_Token);
      Body     : constant String := Build_Request (Sql, Params);
      Response : constant String := Http_Client.Post (Url, Body, Token);
   begin
      if Ada.Strings.Fixed.Index (Response, """type"":""error""") > 0 then
         Put_Line ("Turso error response: " & Response);
         return Empty_Rows;
      end if;
      return Parse_Rows (Response);
   end Fetch_All;

end Turso_Client;
