(*
-std=turbo: real Turbo Pascal does not FORBID the ISO 7185 program-heading
syntax `program p(input, output);`, it simply does not require it -- so a
Turbo program that writes it anyway must still compile and run cleanly.
This is the collision-risk case Sema::registerBuiltins' Input/Output
comment (and the Prog.FileParams loop's own comment, Sema.cpp) both call
out by name: 'input'/'output' are ALREADY predefined Vars by the time the
program heading's own file-parameter loop runs, and that loop has to
recognize them as already-declared rather than report a spurious
err_duplicate_param on a program that did nothing wrong.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:heading listed input and output; no duplicate-parameter error
CHECK-NEXT:eof=TRUE
*)

program HeadingListsInputOutput(input, output);
begin
  Writeln('heading listed input and output; no duplicate-parameter error');
  Writeln('eof=', eof);
end.
