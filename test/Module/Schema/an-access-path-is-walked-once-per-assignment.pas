(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:next called 1 time(s); [hi]
*)

program p(output);
type t(n: integer) = record a: array[1..n] of record s: string(n) end end;
var q: ^t; calls: integer;
function next: integer;
begin calls := calls + 1; next := 1 end;
begin
  calls := 0; new(q, 8);
  q^.a[next].s := 'hi';
  writeln('next called ', calls:1, ' time(s); [', q^.a[1].s, ']');
  dispose(q)
end.
