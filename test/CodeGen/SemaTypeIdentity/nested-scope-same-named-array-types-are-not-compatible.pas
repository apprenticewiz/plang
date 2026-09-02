(*
Issue #598: a NAMED array type-denoter is identified by its declaration (ISO
§6.4.2.3, §6.4.3.2 -- issue #178), not by its spelling.  Two DIFFERENT
declarations that happen to share both a name and a shape -- one at program
scope, an unrelated one nested inside a procedure -- are still not one
another; fpc -Mtp agrees ("got P.T expected P.Q.T" / similar scoped-identity
rejection).  Before this fix, falling through to a purely structural
comparison once the two Type objects failed the `&Dst == &Src` identity
shortcut let this assignment through.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign
*)

program p;
type T = array[1..2] of integer;
var outer: T;
procedure q;
type T = array[1..2] of integer;
var inner: T;
begin
  outer := inner
end;
begin end.
