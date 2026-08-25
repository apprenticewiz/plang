(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:z 1 2 3
*)

program p(output);
type t = record a, b, c: integer end;
     outert = t;
     pt = ^t;
var g: pt;
procedure inner(q: pt);
type t = record ch: char end;
var local: t; r: outert;
begin local.ch := 'z'; r.a := 1; r.b := 2; r.c := 3;
  writeln(local.ch, ' ', r.a, ' ', r.b, ' ', r.c) end;
begin new(g); inner(g) end.
