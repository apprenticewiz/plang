(*
Turbo Tier 4, Cluster A item 1, requirement 3: a unit's interface section
and implementation section have SEPARATE 'uses' clauses with separate
visibility.  Helper's interface exports HelperConst; UsesLeak's own
IMPLEMENTATION 'uses' Helper, but its INTERFACE never does -- so
HelperConst must not be visible while Sema checks UsesLeak's interface
section (Sema::checkUnitInterfaceOnly only ever pushes InterfaceUses, never
ImplementationUses -- see Sema.cpp's own comment on checkUnitInterfaceOnly
and on checkUnit's own scope nesting).  See the sibling test
implementation-uses-sees-both-its-own-uses-and-the-interfaces.pas for the
positive half of this same requirement.

RUN: split-file %s %t.dir
RUN: not %plang -std=turbo -I%t.dir -dump-ast %t.dir/UsesLeak.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: error: undefined identifier 'HelperConst'
*)

//--- helper.pas
unit Helper;
interface
const HelperConst = 5;
implementation
end.

//--- UsesLeak.pas
unit UsesLeak;
interface
const Bad = HelperConst + 1;
implementation
uses Helper;
end.
