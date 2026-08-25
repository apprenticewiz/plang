(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  4.0
*)

program p(output);
type num = record kind: integer;
       case tag: integer of 1: (c: real); 2: (e: integer) end;
var r: num;
begin r.kind := 1; r.tag := 1; with r do c := 4; writeln(r.c:5:1) end.
