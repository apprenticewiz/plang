(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
type e = integer;
     v(n: integer) = array[1..n] of e;
procedure alloc;
type e = char;
var h: v(3);
begin h[1] := 42; writeln(h[1]:1) end;
begin alloc end.
