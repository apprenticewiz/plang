(*
Turbo Tier 5, Cluster A item 4: a method's own (Pascal-declared) parameters
arrive AFTER the implicit 'Self' argument -- Self is prepended, never
counted as one of the method's own params -- so the ordinary argument-
marshalling loop (reused from CGFuncCall::emitUserFuncCall /
CGProcCall::emitUserProcCall) still lines a call's actual arguments up with
the right formal.  A bug that counted Self as parameter 0 (or forgot to
prepend it at the CALL site while the DEFINITION still expects it) would
scramble every argument by one position -- this multi-parameter,
mixed-with-a-field call is what would show that.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TAccumulator = object
    Total: Integer;
    procedure Reset;
    procedure AddThree(A, B, C: Integer);
    function GetTotal: Integer;
    function Scale(Factor: Integer): Integer;
  end;

procedure TAccumulator.Reset;
begin
  Total := 0;
end;

procedure TAccumulator.AddThree(A, B, C: Integer);
begin
  Total := Total + A + B + C;
end;

function TAccumulator.GetTotal: Integer;
begin
  GetTotal := Total;
end;

function TAccumulator.Scale(Factor: Integer): Integer;
begin
  Scale := Total * Factor;
end;

var
  acc: TAccumulator;
begin
  acc.Reset;
  acc.AddThree(1, 2, 3);
  writeln(acc.GetTotal());
  writeln(acc.Scale(10));
end.

(*
CHECK:6
CHECK-NEXT:60
*)
