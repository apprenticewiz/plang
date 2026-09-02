(*
Issue #698: a unit's interface section can declare
'procedure Foo(X: Integer);' while its implementation section defines
'procedure Foo(X: LongInt)' -- a signature mismatch -- with nothing ever
comparing the two: the implementation's own Proc symbol simply shadowed the
interface's, unchecked, in the more deeply nested scope checkBlock gives the
implementation's own top-level declarations, and the interface's own
heading was never actually defined, so every CALLER (not the unit itself)
hit an undefined-symbol link failure instead of this unit's own compile
being rejected.  checkUnitImplConformance (Sema.cpp) now compares each
interface heading against what the implementation defined for the same name
while that scope is still open.

RUN: not %plang -std=turbo -c %s -o %t.o 2> %t.err
RUN: FileCheck %s < %t.err
*)

unit MismatchedUnit;

interface

procedure Foo(X: Integer);

implementation

procedure Foo(X: LongInt);
begin
  writeln(X);
end;

end.

(*
CHECK: error: parameter 'X' of 'Foo': type 'LongInt' does not match its interface heading's type 'integer'
*)
