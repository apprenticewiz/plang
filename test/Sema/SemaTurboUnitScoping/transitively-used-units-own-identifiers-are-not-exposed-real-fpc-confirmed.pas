(*
Turbo Tier 4, Cluster A item 2's own transitive-loading semantics, confirmed
against real `fpc -Mtp` for this item (see its own report): 'uses A' where
A's own interface 'uses B' means only that A's own interface-section
declarations, which may reference B's types/constants, remain well-formed --
it does NOT transitively expose B's OWN identifiers to whoever merely
'uses'd A.  A program that wants B's own CB has to 'uses' B itself.

fpc -Mtp reproduction for this item's own report:
  unitb.pas: unit UnitB; interface const CB = 42; implementation end.
  unita.pas: unit UnitA; interface uses UnitB; const CA = CB + 1; implementation end.
  prog.pas:  program Prog; uses UnitA; begin writeln(CA); writeln(CB) end.
  $ fpc -Mtp prog.pas
  ...
  prog.pas(6,11) Error: Identifier not found "CB"
(CA, which A's own interface computed FROM CB, resolves fine -- only CB
itself, reached directly through C's own 'uses A', does not.)

RUN: split-file %s %t.dir
RUN: not %plang -std=turbo -I%t.dir %t.dir/prog.pas 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: error: undefined identifier 'CB'
*)

//--- unitb.pas
unit UnitB;
interface
const CB = 42;
implementation
end.

//--- unita.pas
unit UnitA;
interface
uses UnitB;
const CA = CB + 1;
implementation
end.

//--- prog.pas
program Prog;
uses UnitA;
begin
  Writeln(CA);
  Writeln(CB);
end.
