(*
Issue #574: 'inherited MethodName' always resolves to a STATIC call to the
direct ancestor's own body (docs/turbo.md's "Object types" section) -- but
an 'abstract' method has no body at all, only CodeGenProcs.cpp's
emitAbstractMethodStub, which unconditionally traps Runtime error 211.
Previously unchecked, this compiled clean and only trapped at RUN TIME
whenever the 'inherited' call actually executed.  Confirmed against a local
fpc -Mtp build, which refuses this at COMPILE time instead ("Abstract
methods cannot be called directly").
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program InheritedAbstractMethod;

type
  TA = object
    constructor Init;
    procedure M; virtual; abstract;
  end;
  TB = object(TA)
    procedure M; virtual;
  end;

constructor TA.Init;
begin
end;

procedure TB.M;
begin
  inherited M;
end;

begin
end.

(*
CHECK: error: cannot call abstract method 'TA.M' via 'inherited'
*)
