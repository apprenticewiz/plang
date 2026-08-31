(*
Issue #511: the same gap stampVptr/stampFieldVptrs' own record case fixes
applies identically to an ARRAY of object type -- each element is its own
subobject, embedded by value in the array's storage, and needs its own
'_vptr' stamped exactly like a record field does.  Confirmed empirically
(the issue itself asked this be checked) that this is the SAME underlying
problem and is covered by the identical mechanism: stampFieldVptrs walks an
Array's ElemType the same way it walks a Record's fields.  Three elements,
unrolled by codegen (stampFieldVptrs' own array branch stays unrolled for a
small, compile-time-constant count, same threshold as emitInitialState's
own array branch) rather than a runtime loop -- both shapes share the same
underlying stamping logic, so this does not separately re-prove the runtime-
loop form.  Confirmed against a local `fpc -Mtp` build: identical three
'woof' lines, exit 0.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program arrobj;
type
  TAnimal = object
    constructor Init;
    procedure Speak; virtual;
  end;
  TDog = object(TAnimal)
    procedure Speak; virtual;
  end;
var
  Pack: array[1..3] of TDog;
  I: Integer;
constructor TAnimal.Init; begin end;
procedure TAnimal.Speak; begin WriteLn('generic'); end;
procedure TDog.Speak; begin WriteLn('woof'); end;
begin
  for I := 1 to 3 do Pack[I].Init;
  for I := 1 to 3 do Pack[I].Speak;
end.

(*
CHECK:woof
CHECK-NEXT:woof
CHECK-NEXT:woof
*)
