(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello]
*)

program p(output);
type t(n: integer) = record inner: record s: string(n) end end;
var q: ^t;
begin
  new(q, 20);
  q^.inner.s := 'hello world';
  writeln('[', q^.inner.s[1..5], ']');
  dispose(q)
end.
