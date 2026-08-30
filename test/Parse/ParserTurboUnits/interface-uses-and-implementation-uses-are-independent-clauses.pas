(*
Turbo's 'uses' clause appears once per section, and the two are a genuinely
separate scope from each other (UnitNode::InterfaceUses vs
UnitNode::ImplementationUses) -- confirmed against real 'fpc -Mtp', which
accepts a unit whose interface uses one set of units and whose
implementation uses an entirely different (even overlapping) set.  Tier 4's
own goal of closing mutual dependence through the implementation uses
clause needs the two kept apart; this only checks that the parser keeps
them apart, not that Sema resolves either (out of scope for this item).
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

unit Mixer;

interface

uses UnitA, UnitB;

implementation

uses UnitB, UnitC;

end.

(*
CHECK:(unit Mixer
CHECK-NEXT:  (interface (uses UnitA UnitB)
CHECK-NEXT:    ())
CHECK-NEXT:  (implementation (uses UnitB UnitC)
CHECK-NEXT:    ())
CHECK-NEXT:)
*)
