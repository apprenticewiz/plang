(*
Turbo Tier 4, Cluster A item 1, requirement 3, positive half (see the
sibling test interface-uses-does-not-see-implementation-uses.pas for the
negative half): a unit's IMPLEMENTATION section sees BOTH its own
interface's declarations (IfaceConst, given to it the same way EP's own
processModuleBody gives an implementation module its interface's
declarations) AND whatever its own 'implementation uses' clause brings in
(HelperConst, from Helper) -- nested one level further in than
InterfaceUses, per Sema::checkUnit's own comment (Sema.cpp).  The
initialization block (ImplConst is read there, not just declared) is
checked in that same scope too.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -dump-ast %t.dir/UsesOk.pas
*)

//--- helper.pas
unit Helper;
interface
const HelperConst = 5;
implementation
end.

//--- UsesOk.pas
unit UsesOk;
interface
const IfaceConst = 10;
implementation
uses Helper;
const ImplConst = HelperConst + IfaceConst;
begin
  if ImplConst = HelperConst + IfaceConst then ;
end.
