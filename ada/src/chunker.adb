-- chunker.adb
-- Implementation of the sliding-window text chunker.
--
-- Algorithm:
--   stride  = Chunk_Size * 3 / 4   (25 % overlap between successive chunks)
--   For each position P in [Text'First, Text'Last] stepping by stride:
--     window = Text[P .. min(P + Chunk_Size - 1, Text'Last)]
--     If the window does not end on a word boundary we walk back to the last
--     space character to avoid splitting a word mid-token.
--
-- WHY 25 % overlap: losing context at chunk boundaries degrades retrieval
-- quality.  25 % is a common empirical compromise between redundancy and
-- coverage.

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;

package body Chunker is

   function Chunk
     (Text       : String;
      Source     : String;
      Chunk_Size : Positive) return String_Array
   is
      -- Work buffer: at most Max_Chunks chunks.
      Buffer : String_Array (1 .. Max_Chunks);
      Count  : Chunk_Index := 1;

      -- Stride = 75 % of Chunk_Size, ensuring 25 % overlap.
      Stride : constant Positive := Integer'Max (1, (Chunk_Size * 3) / 4);

      Pos   : Integer := Text'First;
      Stop  : Integer;
      Break : Integer;

      -- Metadata prefix shared by all chunks from this source.
      Prefix : constant String := "[source:" & Source & "]" & ASCII.LF;
   begin
      while Pos <= Text'Last and then Count <= Max_Chunks loop
         Stop := Integer'Min (Pos + Chunk_Size - 1, Text'Last);

         -- Walk back from Stop to the last space so we do not split a word.
         -- WHY: embedding a partial word ("embeddin") degrades vector quality.
         -- We only do this when the chunk would not otherwise reach the end.
         if Stop < Text'Last then
            Break := Stop;
            while Break > Pos and then Text (Break) /= ' ' and then
              Text (Break) /= ASCII.LF
            loop
               Break := Break - 1;
            end loop;
            -- Only use the adjusted boundary if it is not too short.
            if Break > Pos + (Chunk_Size / 4) then
               Stop := Break;
            end if;
         end if;

         -- Assemble the chunk: metadata prefix + the text window.
         Buffer (Count) := To_Unbounded_String (Prefix & Text (Pos .. Stop));
         Count := Count + 1;
         Pos   := Pos + Stride;
      end loop;

      -- Return only the filled portion of the buffer.
      if Count = 1 then
         -- Text was non-empty (guaranteed by Pre) but shorter than Chunk_Size:
         -- return it as a single chunk.
         Buffer (1) := To_Unbounded_String (Prefix & Text);
         return Buffer (1 .. 1);
      end if;

      return Buffer (1 .. Count - 1);
   end Chunk;

end Chunker;
