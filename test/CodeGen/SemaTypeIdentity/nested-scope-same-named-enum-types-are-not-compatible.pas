(*
Issue #598: the Enum sibling of
nested-scope-same-named-array-types-are-not-compatible.pas.  Each enumerated-
type definition is its own distinct type (ISO §6.4.2.3); two DIFFERENT
declarations sharing a name and a value list -- one at program scope, an
unrelated one nested inside a procedure -- are still not one another.  Before
this fix, comparing only Name and EnumValues (never the declaration) let this
assignment through once the two Type objects failed the `&Dst == &Src`
identity shortcut.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign
*)

program p;
type T = (Red, Green);
var outer: T;
procedure q;
type T = (Red, Green);
var inner: T;
begin
  outer := inner
end;
begin end.
