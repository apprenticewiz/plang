(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
const k = 10;
type v(n: integer) = array[1..n*k] of integer;
procedure alloc;
const k = 1;
var h: v(2);
begin h[20] := 42; writeln(h[20]:1) end;
begin alloc end.
