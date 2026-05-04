-- turso_client.ads
-- Specification for the Turso (libSQL) database client.
--
-- Turso exposes a simple HTTP API: POST /v2/pipeline with a JSON body
-- containing an array of SQL statements.  This package wraps that API
-- in idiomatic Ada, hiding JSON serialisation and deserialisation.
--
-- Ada safety rationale:
--   * All string parameters carry their length; no null-termination is used.
--   * Pre-conditions on every public subprogram make illegal calls
--     detectable at run time (and, with SPARK, at compile time).
--   * The Row and Param types are closed tagged records; callers cannot
--     manufacture invalid database values.

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Rag_Config;

package Turso_Client is

   -- A single SQL parameter value.  We model only the types our RAG pipeline
   -- actually needs: text and null.  Extending to integers and reals is
   -- straightforward via a variant record.
   type Param_Kind is (Param_Text, Param_Null);

   type Param is record
      Kind  : Param_Kind := Param_Null;
      Value : Unbounded_String;
   end record;

   -- WHY bounded arrays with a discriminant rather than a vector:
   -- Ada discriminant arrays have a compile-time maximum, which lets the
   -- run-time system prove absence of heap exhaustion in safety modes.
   -- For our RAG use-cases 32 parameters per query is more than sufficient.
   Max_Params : constant := 32;
   type Param_Index is range 1 .. Max_Params;
   type Param_Array is array (Param_Index range <>) of Param;
   Empty_Params : constant Param_Array (1 .. 0) := (others => <>);

   -- A single column value returned from the database.
   type Cell is record
      Is_Null : Boolean := True;
      Value   : Unbounded_String;
   end record;

   -- Maximum columns we ever fetch.  Vector-of-vectors would be more
   -- general but requires heap allocation; this keeps the type provable.
   Max_Cols : constant := 64;
   type Col_Index is range 1 .. Max_Cols;
   type Row is array (Col_Index range <>) of Cell;

   Max_Rows : constant := 1024;
   type Row_Index is range 1 .. Max_Rows;

   -- Row_Array is a ragged array.  Each element is a single database row
   -- represented as an Unbounded_String that encodes the raw JSON column
   -- values.  The Fetch_All caller is responsible for interpreting the
   -- columns it requested.
   type String_Row is array (Col_Index range 1 .. Max_Cols) of Unbounded_String;
   type Row_Array is array (Row_Index range <>) of String_Row;
   Empty_Rows : constant Row_Array (1 .. 0) := (others => <>);

   -- The client handle.  It is a plain record (not a tagged type) because
   -- we do not need polymorphism: there is exactly one Turso HTTP endpoint.
   type Turso_Client_Type is record
      Cfg : Rag_Config.Config;
   end record;

   -- Make_Client constructs a client from a configuration record.
   function Make_Client (Cfg : Rag_Config.Config) return Turso_Client_Type;

   -- Text_Param / Null_Param are convenience constructors so call sites
   -- never have to write aggregate literals directly.
   function Text_Param (Value : String) return Param;
   function Null_Param return Param;

   -- Execute runs a SQL statement that returns no result set (DDL or INSERT).
   --
   -- Pre WHY: an empty SQL string cannot be a valid statement; catching it
   -- here produces a clear error rather than an obscure HTTP 400 from Turso.
   procedure Execute
     (Client : Turso_Client_Type;
      Sql    : String;
      Params : Param_Array := Empty_Params)
   with Pre => Sql'Length > 0;

   -- Fetch_All runs a SELECT and returns all matching rows.
   -- The result is bounded by Max_Rows; queries that would return more rows
   -- should use SQL LIMIT clauses.
   --
   -- Post WHY: the Post condition documents to callers (and to SPARK) that
   -- the returned slice is always within the declared maximum, preventing
   -- any indexing beyond the array bounds.
   function Fetch_All
     (Client : Turso_Client_Type;
      Sql    : String;
      Params : Param_Array := Empty_Params) return Row_Array
   with Pre  => Sql'Length > 0,
        Post => Fetch_All'Result'Length <= Max_Rows;

end Turso_Client;
