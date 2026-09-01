(*
Issue #619: getOrCreateVmt used to emit EVERY object type's VMT as
InternalLinkage unconditionally, so every translation unit that needed one
(the declaring unit itself, AND every other unit/program that merely USES
the type) materialized its own private copy under the identical mangled
name -- dispatch stayed correct (every copy was content-identical), but the
canonical TP7 type-identity idiom, TypeOf(x) = TypeOf(T), silently lied:
comparing two VMT ADDRESSES that differ only because one was read from this
translation unit's own local copy and the other from a different one's.

Unit W declares TW (one virtual method) and a maker function that
allocates and constructs a TW entirely INSIDE W's own compiled code -- so
W's own translation unit is the one that must DEFINE TW's real VMT
(ensureOwnedVmtsDefined), not merely reference it.  The program only USES
TW (via 'uses W'), reaching an instance solely through MakeW's returned
pointer, and asks whether that instance's own runtime type equals TypeOf
(TW) -- both sides of that comparison must resolve to the SAME external
VMT symbol, or this answers DIFFERENT instead of SAME.

Both compiled entirely separately (the unit's source is deleted before the
program is compiled), matching this project's own established "strongest
possible proof" separate-compilation pattern.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -c %t.dir/unitw.pas -o %t.dir/unitw.o
RUN: rm %t.dir/unitw.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/prog.pas %t.dir/unitw.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TW.Speak
CHECK-NEXT:SAME
*)

//--- unitw.pas
unit UnitW;

interface

type
  TW = object
    constructor Init;
    procedure Speak; virtual;
  end;
  PW = ^TW;

function MakeW: PW;

implementation

constructor TW.Init;
begin
end;

procedure TW.Speak;
begin
  writeln('TW.Speak');
end;

function MakeW: PW;
var
  P: PW;
begin
  New(P, Init);
  MakeW := P;
end;

end.

//--- prog.pas
program VmtIdentityAcrossUnits;

uses UnitW;

var
  P: PW;
begin
  P := MakeW;
  P^.Speak;
  if TypeOf(P^) = TypeOf(TW) then
    writeln('SAME')
  else
    writeln('DIFFERENT');
end.
