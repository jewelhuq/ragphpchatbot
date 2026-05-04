-- vector_math.adb
-- Implementation of vector similarity and serialisation helpers.

with Ada.Numerics.Elementary_Functions; use Ada.Numerics.Elementary_Functions;
with Ada.Strings.Unbounded;             use Ada.Strings.Unbounded;
with Ada.Strings.Fixed;

package body Vector_Math is

   ---------------------------------------------------------------------------
   -- Cosine_Similarity
   ---------------------------------------------------------------------------

   function Cosine_Similarity (A, B : Float_Array) return Float is
      Dot     : Float := 0.0;
      Norm_A  : Float := 0.0;
      Norm_B  : Float := 0.0;
      Denom   : Float;
   begin
      -- Compute dot product and L2 norms in a single pass.
      -- WHY single pass: it is cache-friendly and avoids allocating a
      -- temporary array for intermediate products.
      for I in A'Range loop
         declare
            -- Map B's index to match A's base index.
            J : constant Positive := B'First + (I - A'First);
         begin
            Dot    := Dot    + A (I) * B (J);
            Norm_A := Norm_A + A (I) * A (I);
            Norm_B := Norm_B + B (J) * B (J);
         end;
      end loop;

      Denom := Sqrt (Norm_A) * Sqrt (Norm_B);

      -- Guard against zero-vector inputs (the Pre condition requires non-empty
      -- arrays but does not rule out all-zero vectors).
      if Denom < Float'Epsilon then
         return 0.0;
      end if;

      -- Clamp to [-1.0, 1.0] to absorb floating-point rounding errors that
      -- might otherwise cause the Post condition to fire.
      return Float'Max (-1.0, Float'Min (1.0, Dot / Denom));
   end Cosine_Similarity;

   ---------------------------------------------------------------------------
   -- Float_To_Json
   ---------------------------------------------------------------------------

   function Float_To_Json (V : Float_Array) return String is
      Result : Unbounded_String;
   begin
      Append (Result, "[");
      for I in V'Range loop
         if I > V'First then
            Append (Result, ",");
         end if;
         -- Ada's Float'Image always includes a leading space; trim it.
         declare
            S : constant String := Float'Image (V (I));
         begin
            if S (S'First) = ' ' then
               Append (Result, S (S'First + 1 .. S'Last));
            else
               Append (Result, S);
            end if;
         end;
      end loop;
      Append (Result, "]");
      return To_String (Result);
   end Float_To_Json;

   ---------------------------------------------------------------------------
   -- Json_To_Float
   ---------------------------------------------------------------------------

   function Json_To_Float (Json : String; Expected_Len : Positive)
     return Float_Array
   is
      Result : Float_Array (1 .. Expected_Len) := (others => 0.0);
      Count  : Natural := 0;
      C      : Natural;
      Start  : Natural;
   begin
      -- Locate the opening bracket.
      C := Ada.Strings.Fixed.Index (Json, "[");
      if C = 0 then
         return (1 .. 0 => 0.0);
      end if;
      C := C + 1;

      while C <= Json'Last and then Json (C) /= ']' loop
         -- Skip whitespace and commas.
         while C <= Json'Last and then
           (Json (C) = ' ' or else Json (C) = ',' or else
            Json (C) = ASCII.LF or else Json (C) = ASCII.CR)
         loop
            C := C + 1;
         end loop;
         exit when C > Json'Last or else Json (C) = ']';

         Start := C;
         while C <= Json'Last and then
           (Json (C) in '0' .. '9' or else Json (C) = '.' or else
            Json (C) = '-' or else Json (C) = 'e' or else Json (C) = 'E' or else
            Json (C) = '+')
         loop
            C := C + 1;
         end loop;
         exit when C <= Start;
         exit when Count >= Expected_Len;

         Count := Count + 1;
         begin
            Result (Count) := Float'Value (Json (Start .. C - 1));
         exception
            when Constraint_Error => Result (Count) := 0.0;
         end;
      end loop;

      if Count = 0 then
         return (1 .. 0 => 0.0);
      end if;
      return Result (1 .. Count);
   end Json_To_Float;

end Vector_Math;
