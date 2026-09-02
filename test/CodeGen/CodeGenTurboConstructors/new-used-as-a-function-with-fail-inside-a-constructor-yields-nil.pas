(*
Issue #622: fail-inside-a-constructor-leaves-the-callers-pointer-nil.pas's
own sibling for the FUNCTION form of New -- 'P := New(PDog, Init(...))'
rather than the statement form 'New(P, Init(...))'.  Same contract, proven
end to end through CGProcCall::emitNewObjectValue (the shared allocate/
StampVptr/StampFieldVptrs/emitBoundMethodCall/Fail-unwind-to-nil helper
both New's statement AND function forms now call): a failed constructor
deallocates the partially-constructed object and the expression's own
result is nil, exactly like the statement form's own P.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  PDog = ^TDog;
  TDog = object
    Name: string[20];
    constructor Init(OK: boolean);
  end;

constructor TDog.Init(OK: boolean);
begin
  writeln('Dog.Init entered');
  if not OK then
  begin
    writeln('Dog.Init calling Fail');
    Fail;
  end;
  Name := 'Rex';
  writeln('Dog.Init leaving normally');
end;

var
  P: PDog;
begin
  P := New(PDog, Init(false));
  if P = nil then
    writeln('P is nil after failed Init')
  else
    writeln('P is NOT nil (WRONG)');

  P := New(PDog, Init(true));
  if P = nil then
    writeln('P is nil after successful Init (WRONG)')
  else
    writeln('P is not nil after successful Init: ', P^.Name);
end.

(*
CHECK:Dog.Init entered
CHECK-NEXT:Dog.Init calling Fail
CHECK-NEXT:P is nil after failed Init
CHECK-NEXT:Dog.Init entered
CHECK-NEXT:Dog.Init leaving normally
CHECK-NEXT:P is not nil after successful Init: Rex
*)
