(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[one][two]
*)

program p(output); var a: array[1..2] of string(10);
begin a[1] := 'one'; a[2] := 'two';
 writeln('[', a[1], '][', a[2], ']') end.
