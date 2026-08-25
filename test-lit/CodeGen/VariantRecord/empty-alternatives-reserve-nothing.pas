(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 true
*)

program p(output);
type t = record k: integer;
       case b: boolean of true: (); false: () end;
var z: t;
begin z.k := 4; z.b := true; writeln(z.k, ' ', z.b) end.
