(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcdefghij
*)

program p(output);
type thief = record s: string(20); t: array[1..5] of char end;
var th: thief;
begin th.s := 'abcdefghij'; writeln(th.s[1..10]) end.
