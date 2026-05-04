-- vector_math.ads
-- Specification for vector similarity operations.
--
-- WHY a dedicated package: floating-point operations on high-dimensional
-- vectors are the performance hot-spot of any RAG pipeline.  Isolating them
-- here makes it easy to substitute a BLAS-backed implementation later
-- without touching the rest of the code base.
--
-- Ada safety notes:
--   * Pre conditions enforce equal dimensionality; passing vectors of
--     different lengths would produce a meaningless result without the check.
--   * The result is constrained to [-1.0, 1.0] by the Post condition,
--     matching the mathematical definition of cosine similarity.

with Ollama_Client; use Ollama_Client;

package Vector_Math is

   -- Cosine_Similarity computes the cosine of the angle between two vectors.
   -- Returns a value in [-1.0, 1.0]; higher means more similar.
   --
   -- Pre WHY equal length: cosine similarity is undefined for vectors of
   -- different dimensions.  Detecting the mismatch here prevents a silent
   -- wrong answer that would corrupt retrieval ranking.
   --
   -- Pre WHY non-empty: cosine of the zero vector is undefined (division by
   -- zero); the contract makes the requirement explicit.
   function Cosine_Similarity (A, B : Float_Array) return Float
   with Pre  => A'Length = B'Length and then A'Length > 0,
        Post => Cosine_Similarity'Result >= -1.0
                and then Cosine_Similarity'Result <= 1.0;

   -- Float_To_Json converts a Float_Array to a compact JSON array string
   -- suitable for embedding in a SQL parameter.
   function Float_To_Json (V : Float_Array) return String
   with Pre => V'Length > 0;

   -- Json_To_Float parses a JSON array of numbers into a Float_Array.
   -- Returns a zero-length array on parse failure.
   function Json_To_Float (Json : String; Expected_Len : Positive)
     return Float_Array;

end Vector_Math;
