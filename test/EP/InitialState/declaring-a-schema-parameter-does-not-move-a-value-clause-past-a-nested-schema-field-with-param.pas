(*
Issue #197.  See the -plain sibling for the full explanation.  This program
only adds a procedure with an UNDISCRIMINATED `t` parameter -- never even
called -- which is what makes Sema re-resolve inner(n) (x's type) against a
probe binding instead of a(4)'s.  Output must be identical to -plain.
*)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 0 0 0 99
*)

program p(output);
type inner(m: integer) = array[1..m] of integer;
     t(n: integer) = record x: inner(n); k: integer value 99 end;
var a: t(4);
procedure body(var v: t);
begin
end;
begin
  body(a);
  writeln(a.x[1]:1, ' ', a.x[2]:1, ' ', a.x[3]:1, ' ', a.x[4]:1, ' ', a.k:1)
end.
