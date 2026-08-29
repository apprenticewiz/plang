(*
-std=turbo: Input/Output are now real, addressable text-file VARIABLES
(Sema::registerBuiltins), not merely the ISO/EP file-parameter mechanism's
implicit Vars -- so `Assign(Output, name); Rewrite(Output);` redirects
where a BARE `Writeln` (no file argument at all) actually lands, exactly
the way real Turbo Pascal's own Output redirection idiom works.  This is
the single most important end-to-end behavior this item enables: before
it, a bare write/writeln always went straight to the console through a
special null-file-pointer codepath that could never see a redirection.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: cat bare-writeln-with-no-file-argument-writes-to-a-redirected-output.txt | FileCheck %s
*)

(*
CHECK:hello from a bare writeln
CHECK-NEXT:42
*)

begin
  Assign(Output, 'bare-writeln-with-no-file-argument-writes-to-a-redirected-output.txt');
  Rewrite(Output);
  Writeln('hello from a bare writeln');
  Writeln(42);
  Close(Output);
end.
