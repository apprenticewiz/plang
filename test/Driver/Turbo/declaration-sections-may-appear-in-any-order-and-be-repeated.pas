(*
Turbo needs no code change for this: FreeDeclarationOrder's dialect mask
(LangFeatures.def) already includes D_Turbo alongside D_ISO10206, pre-wired
by an earlier prerequisite before Turbo syntax existed to use it.
Parser::parseBlock's gate (ParseDecl.cpp) reads that mask, so a program
that interleaves var/const/type/procedure sections -- ISO 7185 fixes the
order label, const, type, var and allows one of each -- already compiled
under -std=turbo with zero changes.  This locks that in as a regression
test rather than leaving it to be discovered only by re-deriving it from
LangFeatures.def again.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program declorder;
var x: integer;
procedure Foo;
begin
  writeln('foo');
end;
const c = 42;
type t = integer;
var y: t;
begin
  x := c;
  y := x;
  Foo;
  writeln(y);
end.

(*
CHECK:foo
CHECK-NEXT:42
*)
