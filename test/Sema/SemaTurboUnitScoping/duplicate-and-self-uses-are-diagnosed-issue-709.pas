(*
Issue #709: `uses Base, Base;` (the same unit named twice in one 'uses'
clause) and a unit naming itself in its own 'uses' clause were both
silently accepted -- the first brought nothing new the second time, and the
self-use case, if in the IMPLEMENTATION section, actually loaded a cache
hit of this unit's own not-yet-finished exports (a real, but nonsensical,
self-import), rather than either being diagnosed the way real `fpc -Mtp`
diagnoses both ("Duplicate identifier").

pushUnitUsesScopes (Sema.cpp) now checks both cases itself, gated by a
CheckDuplicatesAndSelf parameter that is false only for checkUnit's own
SECOND pass over Unit.InterfaceUses (rebuilding the same scopes again,
underneath the implementation's) -- checkUnitInterfaceOnly's first pass
already reported anything wrong with that list, so this proves that pass
does NOT also double-report (self-use in the INTERFACE, checked below,
would otherwise fire twice: once from checkUnitInterfaceOnly's own pass,
once again from checkUnit's second pass building the implementation scope).

RUN: split-file %s %t.dir
RUN: not %plang -std=turbo -I%t.dir %t.dir/dup.pas -o %t.dir/dup 2> %t.dup.err
RUN: FileCheck --check-prefix=DUP %s < %t.dup.err
RUN: not %plang -std=turbo -c %t.dir/selfimpl.pas -o %t.dir/selfimpl.o 2> %t.selfimpl.err
RUN: FileCheck --check-prefix=SELFIMPL %s < %t.selfimpl.err
RUN: not %plang -std=turbo -c %t.dir/selfiface.pas -o %t.dir/selfiface.o 2> %t.selfiface.err
RUN: FileCheck --check-prefix=SELFIFACE %s < %t.selfiface.err
// Issue #709's own regression risk: an INTERFACE self-use must be reported
// exactly ONCE, not once per pass over Unit.InterfaceUses (checkUnit's own
// second pass rebuilds the same scopes again for the implementation --
// CheckDuplicatesAndSelf=false's own job is to keep that pass silent).
RUN: grep -c "use itself" %t.selfiface.err | FileCheck --check-prefix=ONCE %s
*)

(*
ONCE: 1
*)

(*
DUP: error: duplicate 'uses': unit 'Base' is already used
SELFIMPL: error: unit 'SelfImpl' may not use itself
SELFIFACE: error: unit 'SelfIface' may not use itself
*)

//--- base.pas
unit Base;
interface
const X = 1;
implementation
end.

//--- dup.pas
program Dup;
uses Base, Base;
begin
end.

//--- selfimpl.pas
unit SelfImpl;
interface
procedure Foo;
implementation
uses SelfImpl;
procedure Foo;
begin
end;
end.

//--- selfiface.pas
unit SelfIface;
interface
uses SelfIface;
procedure Foo;
implementation
procedure Foo;
begin
end;
end.
