(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p(output);
type t(n: integer) = record next: ^t(n); k: integer end;
var v: t(4);
begin v.k := 7; v.next := nil; writeln(v.k:1) end.
