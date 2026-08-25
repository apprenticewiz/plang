(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:lo=1 hi=5: 11 22 33 44 55
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
var q: ^vec; i: integer;
procedure show(var x: array[lo..hi: integer] of integer);
var k: integer;
begin
  write('lo=', lo:1, ' hi=', hi:1, ':');
  for k := lo to hi do write(' ', x[k]:1);
  writeln
end;
begin
  new(q, 5);
  for i := 1 to 5 do q^[i] := i * 11;
  show(q^);
  dispose(q)
end.
