(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 abcde
*)

program p(output);
type buf(n: integer) = record len: integer; d: array[1..n] of char end;
var q: ^buf; i: integer;
begin
  new(q, 5); q^.len := 5;
  for i := 1 to 5 do q^.d[i] := chr(ord('a') + i - 1);
  write(q^.len:1, ' ');
  for i := 1 to 5 do write(q^.d[i]);
  writeln; dispose(q)
end.
