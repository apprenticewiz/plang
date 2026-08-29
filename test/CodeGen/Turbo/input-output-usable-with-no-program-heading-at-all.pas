(*
-std=turbo: the more common real-world shape -- a program with no `program`
heading naming input/output at all (or no heading whatsoever) -- still
gets Input/Output as ordinary, already-there predefined variables, usable
without any declaration.  Sema::registerBuiltins registers both
unconditionally under Turbo, independent of whatever the Prog.FileParams
loop (Sema.cpp) does with an explicit heading.  Also confirms Assign/
Rewrite on Output still works from this shape, not just the trivial
"compiles" claim.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: cat input-output-usable-with-no-program-heading-at-all.txt | FileCheck %s
*)

(*
CHECK:no heading needed
*)

begin
  Assign(Output, 'input-output-usable-with-no-program-heading-at-all.txt');
  Rewrite(Output);
  Writeln('no heading needed');
  Close(Output);
end.
