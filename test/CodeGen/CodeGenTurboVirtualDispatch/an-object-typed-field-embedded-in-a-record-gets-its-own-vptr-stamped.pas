(*
Issue #511: stampVptr (CodeGenProcs.cpp) is the mechanism that writes a
concrete object type's own VMT global address into an instance's '_vptr'
slot -- without it, any virtual call dispatched through that instance reads
garbage.  Before this fix, it was called from exactly one place
(emitVarValueInit), and only for a directly-declared 'var X: TObjectType;'
local or global.  There was no third call site for "an object subobject
nested inside some OTHER type's own storage" -- so a record field of object
type (Pet, below) never had its own '_vptr' stamped at all, and the virtual
call through it (H.Pet.Speak) read a function pointer from whatever garbage
happened to be in H's own storage: a segfault on a global (zero-initialized,
so a null-pointer jump) and on a stack local (indeterminate).

This is the issue's own repro, essentially verbatim.  H.Pet.Init (a
non-virtual constructor call, inherited from TAnimal) runs fine even before
this fix -- it needs no vptr.  The crash was specifically in H.Pet.Speak, a
virtual call.  Confirmed against a local `fpc -Mtp` build: real Turbo/FPC
prints the identical single 'woof' line here (TDog's own override, since
H.Pet's declared/concrete type is TDog, not TAnimal) with exit 0.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program recfield;
type
  TAnimal = object
    constructor Init;
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;
  THolder = record
    Pet: TDog;
    Label_: Integer;
  end;
constructor TAnimal.Init; begin end;
procedure TAnimal.Speak; begin WriteLn('generic'); end;
procedure TDog.Speak; begin WriteLn('woof'); end;
var
  H: THolder;
begin
  H.Pet.Init;
  H.Pet.Speak;
end.

(*
CHECK:woof
*)
