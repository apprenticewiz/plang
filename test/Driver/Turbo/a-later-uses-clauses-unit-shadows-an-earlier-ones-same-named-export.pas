(*
Turbo Tier 4, Cluster A item 1's own core claim: two 'uses'd units (UnitA,
UnitB) each export a constant called Greeting with a DIFFERENT value, and a
bare read of Greeting resolves to whichever unit was named LAST in the
'uses' clause -- proven at RUNTIME, not just that it type-checks, since this
is the one place a compile-only check could not tell "resolved to the right
symbol" apart from "resolved to a coincidentally-identical value".

This falls out of Sema::pushUnitUsesScopes pushing one SymbolTable scope per
'uses'd unit, in order, with no change to SymbolTable::define's clash policy
at all: the two units' exports never share a scope to clash in, so
SymbolTable::lookup's ordinary innermost-first search finds UnitB's Greeting
first, UnitB having been pushed later (more innermost).  Confirmed against
real `fpc -Mtp` with the same two units and the same three Writeln calls
(see this item's own report) -- fpc prints 2, 1, 2, exactly like this.

The narrow codegen this needs (Codegen::setUsedUnits) only folds a SCALAR
constant into a compile-time immediate; that is exactly what Greeting is
here.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:1
CHECK-NEXT:2
*)

//--- unita.pas
unit UnitA;
interface
const Greeting = 1;
implementation
end.

//--- unitb.pas
unit UnitB;
interface
const Greeting = 2;
implementation
end.

//--- main.pas
program Shadow;
uses UnitA, UnitB;
begin
  Writeln(Greeting);
  Writeln(UnitA.Greeting);
  Writeln(UnitB.Greeting);
end.
