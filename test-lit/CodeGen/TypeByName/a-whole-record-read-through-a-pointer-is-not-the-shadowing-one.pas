(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22 33
*)

program p(output);
type rec = record a, b, c: integer end;
var src: ^rec; dst: rec;
procedure q;
type rec = record z: char end;
var l: rec;
begin l.z := 'q'; dst := src^ end;
begin new(src); src^.a := 11; src^.b := 22; src^.c := 33;
  q; writeln(dst.a, ' ', dst.b, ' ', dst.c) end.
