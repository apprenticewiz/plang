(*
Turbo Tier 5, Cluster A item 7: what a VMT slot for an ABSTRACT method that
no descendant ever overrides actually contains, and what happens when it is
called anyway.  Confirmed against a local fpc -Mtp build (abs2.pas):
instantiating (and even calling a non-abstract method on) an object whose
hierarchy leaves an abstract method unoverridden compiles and links clean
(fpc gives only a warning, "Constructing a class ... with abstract method
..."), and ACTUALLY CALLING the abstract method through the VMT exits with
"Runtime error 211" (FPC's own numbered code for "Call to abstract
method").  Before this item, the analogous plang program did not compile
at all -- Codegen::Impl::getOrCreateVmt had no function to put in the slot
and hit its own internal-error guard, a hard compiler crash for a program
real Borland/FPC accepts.  emitAbstractMethodStub (CodeGenProcs.cpp) now
synthesizes a real, defined trap-body function for the slot instead.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: not %run %t.O0 > %t.O0.out 2> %t.O0.err
RUN: FileCheck %s < %t.O0.out
RUN: FileCheck --check-prefix=ERR %s < %t.O0.err
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: not %run %t.O2 > %t.O2.out 2> %t.O2.err
RUN: FileCheck %s < %t.O2.out
RUN: FileCheck --check-prefix=ERR %s < %t.O2.err
*)

program AbstractMethodTrap;

type
  TShape = object
    constructor Init;
    function Area: real; virtual; abstract;
    procedure Show;
  end;

constructor TShape.Init;
begin
end;
procedure TShape.Show;
var V: real;
begin
  V := Self.Area();
  writeln('area=', V:0:2);
end;

var
  S: TShape;
begin
  S.Init;
  writeln('calling abstract method now');
  S.Show;
  writeln('unreachable');
end.

(*
CHECK:calling abstract method now
CHECK-NOT:unreachable
ERR:Runtime error 211
*)
