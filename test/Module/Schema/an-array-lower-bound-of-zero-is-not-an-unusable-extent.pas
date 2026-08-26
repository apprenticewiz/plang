(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 16
*)

program p(output);
type vec(lo, hi: integer) = array[lo..hi] of integer;
var v: ^vec; i: integer;
begin new(v, 0, 4);
      for i := 0 to 4 do v^[i] := i * i;
      writeln(v^[0]:1, ' ', v^[4]:1); dispose(v) end.
