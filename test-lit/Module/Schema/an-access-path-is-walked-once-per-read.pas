(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:next called 1 time(s); [hello]
*)

//--- test.pas
program p(input, output);
type t(n: integer) = record a: array[1..n] of record s: string(n) end end;
var q: ^t; calls: integer;
function next: integer;
begin calls := calls + 1; next := 1 end;
begin
  calls := 0; new(q, 8);
  read(q^.a[next].s);
  writeln('next called ', calls:1, ' time(s); [', q^.a[1].s, ']');
  dispose(q)
end.

//--- stdin.txt
hello
