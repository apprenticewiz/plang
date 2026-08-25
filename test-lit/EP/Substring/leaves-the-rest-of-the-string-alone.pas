(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:zzzdef 6
*)

program p(output);
var s: string(10); i: integer;
begin s := 'abcdef';
  for i := 1 to 3 do s[i..i] := 'z';
  writeln(s, ' ', length(s)) end.
