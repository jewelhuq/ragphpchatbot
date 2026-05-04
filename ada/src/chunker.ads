-- chunker.ads
-- Specification for the text chunking package.
--
-- WHY chunking: large language models have a finite context window.  A
-- document of 50 000 characters cannot be embedded as one unit; we split it
-- into overlapping windows that each fit comfortably in the model's input.
--
-- Ada safety notes:
--   * The Pre condition on Chunk prevents calling the function on empty text,
--     which would silently produce a zero-element array and confuse the
--     ingestion pipeline.
--   * String_Array uses Positive indices so the compiler can prove that every
--     loop over the result visits at least one element (given a non-empty
--     input).

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;

package Chunker is

   Max_Chunks : constant := 4096;
   type Chunk_Index is range 1 .. Max_Chunks;

   -- String_Array holds up to Max_Chunks Unbounded_Strings.
   -- We use Unbounded_String rather than fixed String because chunk lengths
   -- vary and we do not want to waste stack space on worst-case padding.
   type String_Array is array (Chunk_Index range <>) of Unbounded_String;

   -- Chunk splits Text into overlapping windows of at most Chunk_Size
   -- characters.  Each window is returned as an Unbounded_String that
   -- includes a metadata prefix of the form "[source:<Source>]\n".
   --
   -- Pre WHY: splitting empty text is meaningless; the contract makes the
   -- requirement explicit and gives a clear error at the call site rather
   -- than a confusing empty result or a division-by-zero inside the body.
   --
   -- Pre WHY Chunk_Size: Positive already excludes zero and negative values
   -- at the type level; no additional check is needed.
   function Chunk
     (Text       : String;
      Source     : String;
      Chunk_Size : Positive) return String_Array
   with Pre  => Text'Length > 0,
        Post => Chunk'Result'Length >= 1
                and then Chunk'Result'Length <= Max_Chunks;

end Chunker;
