(*
Exercises BuiltinIO.cpp's ShortString field-width branch (plang_sstr_write_w
in the new runtime/plang_sstr.cpp), the width-aware twin of the plain
read-write-round-trip test's no-width path.  ISO §6.10.3.6's field-width
rule -- a field is exactly w characters, right-justified, truncating a
longer value -- is not itself new Turbo Pascal string[N] semantics (the
identical rule already applies to every other write-parameter this project
supports), so giving ShortString the same field-width support VarString and
every scalar already have is squarely inside this item's "does not crash,
produces sensible output" scope, not the later, separate item for TP's
string-specific truncating-ASSIGNMENT/comparison semantics.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:      hi
CHECK-NEXT:hi
*)

//--- test.pas
program p(input, output);
var s: string[10];
begin
  readln(s);
  writeln(s:8);
  writeln(s:2)
end.

//--- stdin.txt
hi
