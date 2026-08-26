(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of bounds 1..5
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
     vecptr = ^vec;
procedure makeit(var p: vecptr);
var n: integer;
begin n := 999999; new(p, 5); p^[6] := 1 end;
var q: vecptr;
begin makeit(q); writeln('not reached') end.
