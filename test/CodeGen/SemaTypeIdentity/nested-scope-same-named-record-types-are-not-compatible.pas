(*
Issue #598: the Record sibling of
nested-scope-same-named-array-types-are-not-compatible.pas.  A declared
record type is identified by its declaration (ISO §6.4.3.3); two DIFFERENT
declarations sharing a name and a shape -- one at program scope, an unrelated
one nested inside a procedure -- are still not one another.  Before this fix,
RecordDecl was compared only to say "yes, same declaration" and never to say
"no, different declaration" -- once the fast path's RecordDecl pointers
disagreed, control fell through to a purely structural field-by-field
comparison that accepted the identical shape anyway.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign
*)

program p;
type T = record x: integer end;
var outer: T;
procedure q;
type T = record x: integer end;
var inner: T;
begin
  outer := inner
end;
begin end.
