(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p(output);
type fig = record
       case kind: integer of
         1: (r: integer);
         2: (w, h: integer)
     end;
var f: fig;
begin f.w := 2; f.h := 3; writeln(f.r) end.
