(*
Issue #594.  The sibling test
a-later-uses-clauses-unit-shadows-an-earlier-ones-same-named-export.pas
proved last-uses-wins for a `const` (folded to a compile-time immediate by
Sema, never referenced through ImportOwners_ at all) -- this proves the same
rule for a `var` and a `procedure`/`function`, which DO go through
ImportOwners_ (Sema::pushUnitUsesScopes, lib/Sema/Sema.cpp).  That table's
`emplace` was a no-op once the FIRST 'uses'd unit's entry existed, so
CodeGen mangled a bare `Shared`/`Greet` reference against UnitA even though
Symtab.lookup's own scope stack (used for everything else) had already
resolved it to UnitB -- the two disagreed, and CodeGen trusted the wrong
one.  Confirmed against real `fpc -Mtp` with the same units: it prints
222/111/222, matching CHECK below.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/ua.pas -o %t.dir/ua.o
RUN: %plang -std=turbo -I%t.dir -c %t.dir/ub.pas -o %t.dir/ub.o
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/ua.o %t.dir/ub.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Shared=222
CHECK-NEXT:UA.Shared=111
CHECK-NEXT:UB.Shared=222
CHECK-NEXT:Greet=222
CHECK-NEXT:UA.Greet=111
CHECK-NEXT:UB.Greet=222
*)

//--- ua.pas
unit UA;
interface
var Shared: Integer;
function Greet: Integer;
implementation
function Greet: Integer; begin Greet := 111; end;
end.

//--- ub.pas
unit UB;
interface
var Shared: Integer;
function Greet: Integer;
implementation
function Greet: Integer; begin Greet := 222; end;
end.

//--- main.pas
program Main;
uses UA, UB;
begin
  UA.Shared := 111;
  UB.Shared := 222;
  Writeln('Shared=', Shared);
  Writeln('UA.Shared=', UA.Shared);
  Writeln('UB.Shared=', UB.Shared);
  Writeln('Greet=', Greet);
  Writeln('UA.Greet=', UA.Greet);
  Writeln('UB.Greet=', UB.Greet);
end.
