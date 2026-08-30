(*
Turbo Tier 4, Cluster A item 2's own circularity rule: two units' own
IMPLEMENTATION sections may 'uses' each other (UnitA's implementation uses
UnitB, UnitB's implementation uses UnitA) -- real Turbo Pascal allows this,
confirmed against real `fpc -Mtp` for this item (see its own report): by the
time either unit's implementation needs the other, it only needs the
other's already-fully-resolved INTERFACE, never the other's implementation,
so there is no genuine circular DEPENDENCY in what must be known to
type-check, even though the two units' own SOURCE FILES do refer to each
other.  (Two units' own INTERFACE sections mutually 'uses'-ing each other is
still a hard, diagnosed error -- see
test/Sema/SemaTurboUnitScoping/circular-interface-uses-is-diagnosed-not-an-
infinite-recursion.pas.)

This is exercised here as real, separate, per-file compilation -- not one
Sema instance quietly tolerating a graph its own single-process cache
happens not to revisit -- so it is the strongest available proof: UnitA is
compiled entirely on its own (its own `plang -c`, in a process that has
never heard of UnitB except through UnitB's own source, since neither has a
.tui yet the first time around), then UnitB the same way, then a program
using UnitB (which calls into UnitA) is linked against both real objects.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unita.pas -o %t.dir/unita.o
RUN: %plang -std=turbo -I%t.dir -c %t.dir/unitb.pas -o %t.dir/unitb.o
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/unita.o %t.dir/unitb.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:in PB calling PA
CHECK-NEXT:in PA
*)

//--- unita.pas
unit UnitA;
interface
procedure PA;
implementation
uses UnitB;
procedure PA;
begin
  Writeln('in PA');
end;
end.

//--- unitb.pas
unit UnitB;
interface
procedure PB;
implementation
uses UnitA;
procedure PB;
begin
  Writeln('in PB calling PA');
  PA;
end;
end.

//--- main.pas
program MutualImpl;
uses UnitB;
begin
  PB;
end.
