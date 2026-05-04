-- http_client.ads
-- Specification for a minimal HTTP client built on top of the system curl(1)
-- executable.
--
-- WHY curl via subprocess rather than a native Ada HTTP stack:
-- Ada's standard library predates the web; there is no standard HTTP package.
-- The GNAT Community and FSF GNAT distributions ship GNAT.OS_Lib which lets
-- us spawn child processes safely.  Using curl means zero C bindings, zero
-- foreign-function-interface risk, and a well-audited transport layer.
--
-- WHY GNAT.OS_Lib.Spawn rather than Ada.Streams:
-- Spawn gives us a blocking, non-interactive call whose return code we can
-- check.  The alternative (GNAT.Sockets raw TCP) would require us to
-- implement HTTP/1.1 framing, TLS, and chunked transfer encoding -- all
-- of which are out of scope for this reference implementation.

with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;

package Http_Client is

   -- Http_Error is raised when curl exits with a non-zero status or when the
   -- temporary file cannot be read.
   Http_Error : exception;

   -- Post sends an HTTP POST request with a JSON body and an optional Bearer
   -- token, then returns the full response body as a String.
   --
   -- Pre-condition WHY: an empty URL cannot refer to any valid endpoint;
   -- catching this at the contract level prevents a confusing curl error
   -- message deep inside the implementation.
   --
   -- Pre-condition WHY body: sending a POST with no body is legal HTTP but
   -- is never correct for our JSON-over-HTTP APIs; the contract documents
   -- the invariant.
   function Post
     (Url   : String;
      Body  : String;
      Token : String := "") return String
   with Pre => Url'Length > 0 and then Body'Length > 0;

   -- Post_Stream sends an HTTP POST and delivers the response to On_Chunk
   -- incrementally.  For streaming LLM responses each Server-Sent Event line
   -- is delivered as a separate call to On_Chunk.
   --
   -- WHY access procedure rather than a tagged type callback:
   -- Access-to-subprogram types are first-class in Ada and require no heap
   -- allocation, which is important in safety-critical embedded contexts
   -- where dynamic dispatch overhead must be bounded.
   procedure Post_Stream
     (Url      : String;
      Body     : String;
      Token    : String := "";
      On_Chunk : access procedure (Chunk : String))
   with Pre => Url'Length > 0
               and then Body'Length > 0
               and then On_Chunk /= null;

end Http_Client;
