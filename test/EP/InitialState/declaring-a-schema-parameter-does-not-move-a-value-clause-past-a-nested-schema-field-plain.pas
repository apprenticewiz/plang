(*
Issue #197.  emitInitialState's record arm called layoutOf(*rtn) with no
Sema record, so it laid a schema body out from its field DENOTERS alone --
same defect R4 fixed for llvmTypeOfSemaType's record arm (see
declaring-a-schema-parameter-does-not-resize-an-instance-of-it-*.pas), just
in the 'value' clause's own GEP path instead of the storage-allocation one.

x's denoter (inner(n)) is shared by every instantiation of t, and Sema
re-annotates its cached ResolvedBody each time an UNDISCRIMINATED `t` is
resolved elsewhere -- `procedure body(var v: t)` below does exactly that,
probing inner at a size other than 4.  Without semaRec threaded into
layoutOf, emitInitialState sized x from that probe instead of from a(4)'s
own instance, so k's value clause landed at the probe's offset -- inside
x's array -- instead of k's real one.

The two programs must agree, which is the whole assertion: this one has no
undiscriminated mention of t at all, so it is unaffected either way and
gives the baseline the -with-param sibling must match.
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
begin
  writeln(a.x[1]:1, ' ', a.x[2]:1, ' ', a.x[3]:1, ' ', a.x[4]:1, ' ', a.k:1)
end.
