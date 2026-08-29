(*
-std=turbo: Halt(n) exits through plang_halt (runtime/plang_sys.cpp), not
through the ordinary fall-off-the-end-of-the-block path -- this confirms a
redirected Output's buffered content is actually flushed to disk on THAT
path too.  plang_halt used to call std::fflush(stdout) specifically; since
a redirected Output is a wholly separate FILE* Assign/Rewrite opened
itself, only std::fflush(nullptr) (every open C stream) reliably reaches
it here.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: cat halt-flushes-a-redirected-outputs-buffered-content.txt | FileCheck %s
*)

(*
CHECK:written before halt
*)

begin
  Assign(Output, 'halt-flushes-a-redirected-outputs-buffered-content.txt');
  Rewrite(Output);
  Write('written before halt');
  Halt(0);
end.
