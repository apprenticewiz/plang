(*
RUN: not %plang -std=iso10206 -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
function myabs(n: integer): integer;
begin if n < 0 then myabs := -n else myabs := n end;
var x: integer;
procedure q;
type t = 1..myabs(-5);
var y: t;
begin end;
begin end.

(*
CHECK: not a constant expression
*)
