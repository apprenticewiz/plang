(*
Turbo Tier 4, Cluster A item 1: this item's own temporary loader has no
transitive/circularity story of its own (real 'uses' cycle handling across
interface files is item 2's job) -- it only refuses to recurse into a unit
that is already being loaded (Sema::UnitLoading_), so a genuine cycle
between two units' own INTERFACE 'uses' clauses is reported once instead of
overflowing the stack.  (A real Pascal compiler refuses this too: two
units' interfaces cannot mutually depend on each other, only their
IMPLEMENTATIONs can -- Cluster A item 2's own job to make that work for
real.)

RUN: split-file %s %t.dir
RUN: not %plang -std=turbo -I%t.dir -dump-ast %t.dir/CycleA.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: error: circular 'uses': unit 'CycleB' is used while it is still being loaded
*)

//--- CycleA.pas
unit CycleA;
interface
uses CycleB;
const A = 1;
implementation
end.

//--- CycleB.pas
unit CycleB;
interface
uses CycleA;
const B = 2;
implementation
end.
