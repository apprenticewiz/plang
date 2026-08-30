(*
Turbo Tier 4, Cluster A item 1: every Turbo program gets an implicit
'uses System' pushed UNDERNEATH (least-recently-pushed, most-easily
shadowed of) any explicit 'uses' clause (Sema::pushUnitUsesScopes's own
comment).  registerBuiltins() already defines MaxInt as a required
identifier -- Sema::check's very first, global scope, opened before any
'uses' scope -- so it stands in here for "a System-provided identifier": a
real unit that exports its own MaxInt must be able to shadow it, exactly
the way a real unit exporting Random can shadow the required *procedure*
Random under real `fpc -Mtp` (confirmed empirically for this item -- see
its own report: a `function Random: LongInt;` in a used unit's interface,
called as `Writeln(Random)`, prints the unit's own value, not a real
random number).  Symbol::IsRequiredIdentifier's own "replace on collision"
exception is for a require-identifier being redeclared in the SAME scope
(registerBuiltins' own precedent); this is a different mechanism entirely
-- the shadowing unit's MaxInt lives in its OWN scope, one level further in
than the global scope the required MaxInt lives in, so ordinary
innermost-first lookup finds it first with no interaction with that
exception at all.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:12345
*)

//--- shadower.pas
unit Shadower;
interface
const MaxInt = 12345;
implementation
end.

//--- main.pas
program ShadowsSystem;
uses Shadower;
begin
  Writeln(MaxInt);
end.
