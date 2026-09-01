(*
Issue #619 (companion to vmt-identity-is-shared-across-a-unit-boundary-not-
duplicated.pas): getOrCreateVmt only ever ran from code that actually
NEEDS a VMT (stamping an instance's `_vptr`, or a TypeOf/virtual-dispatch
reference) -- entirely legitimate for a unit to declare an object type with
a virtual method purely for OTHER translation units to use, never itself
instantiating or referencing one.  Fixing #619's own linkage bug (exactly
one translation unit DEFINES a shared external VMT, see the sibling test
above) on its own would have made this a WORSE failure than before: with
nothing in UnitW2's own compile ever calling getOrCreateVmt for TW2, the
real external definition would never be emitted ANYWHERE, leaving the
program's own reference to `pas_vmt$unitw2$tw2` undefined at LINK TIME --
a hard failure for a legal program, not silent-wrong-behavior.
ensureOwnedVmtsDefined closes exactly this gap: every object type a block
declares gets its VMT unconditionally defined by that block's own
translation unit, whether or not that unit's own code ever asks for it.

UnitW2 declares TW2 (one virtual method) and calls neither Init nor Speak
anywhere in its own implementation -- the program alone declares a TW2
variable and uses it.  This test's real assertion is that the link
SUCCEEDS at all (RUN would fail outright on an undefined-reference linker
error otherwise); the printed output is incidental confirmation that
dispatch through the resulting VMT still works correctly.

Both compiled entirely separately (the unit's source is deleted before the
program is compiled), matching this project's own established "strongest
possible proof" separate-compilation pattern.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/unitw2.pas -o %t.dir/unitw2.o
RUN: rm %t.dir/unitw2.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main2.pas %t.dir/unitw2.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TW2.Speak
*)

//--- unitw2.pas
unit UnitW2;

interface

type
  TW2 = object
    constructor Init;
    procedure Speak; virtual;
  end;

implementation

constructor TW2.Init;
begin
end;

procedure TW2.Speak;
begin
  writeln('TW2.Speak');
end;

end.

//--- main2.pas
program UnitOnlyExportsAVirtualType;

uses UnitW2;

var
  W: TW2;
begin
  W.Init;
  W.Speak;
end.
