(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 9 [hello]
*)

program p(output);
type t(n: integer) = record
       inner: record s: string(n); k: integer end;
       tail: integer
     end;
var q: ^t;
begin
  new(q, 20);
  q^.inner.s := 'hello'; q^.inner.k := 7; q^.tail := 9;
  writeln(q^.inner.k:1, ' ', q^.tail:1, ' [', q^.inner.s, ']');
  dispose(q)
end.
