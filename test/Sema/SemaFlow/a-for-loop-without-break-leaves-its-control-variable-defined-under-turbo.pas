(*
Issue #659: ISO §6.8.3.9 undefines a for-statement's control variable once
it exhausts its range, but real Turbo Pascal does not -- `fpc -Mtp` leaves
it holding the last value the loop ran with, the same as it already does
when the loop exits via Break (break-leaves-the-for-loops-control-variable-
readable.pas). checkDefiniteAssignment's ForStmt arm (SemaFlow.cpp) must
give a normal (non-Break) exit the identical treatment under -std=turbo:
no warning, and no false "never given a value" either.

The default (non-Turbo) dialect keeps warning -- see
the-control-variable-is-undefined-after-its-for.pas, test/Driver/Warnings.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: undefined here
ERR-ABSENT-NOT: before it has been given a value
*)

program p;
var i: Integer;
begin
  for i := 1 to 10 do
    writeln(i);
  writeln('after: ', i)
end.
