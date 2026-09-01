(*
A gap left by the two commits fixing #543: both regression tests added
there reach ClosureAndCallABI's PARAMETER-relaying siblings
(procParamFnType/flattenProcParams), since ApplyTwice's own "applier" is a
procedural PARAMETER. Neither exercises the other pair,
procVarFnType/emitProcVarCall, which builds/relays a call made through a
procedural VARIABLE whose own signature nests a named procedural
parameter type.

Confirmed empirically that this specific pair is independently
load-bearing, not merely a defensive duplicate of the parameter-side fix:
reverting only procVarFnType/emitProcVarCall's alias-resolving fix (while
leaving procParamFnType/flattenProcParams fixed) reproduces the exact
"Incorrect number of arguments passed to called function!" LLVM
IR-verification ICE this whole issue is about, on this exact program.

MyApplier(AddI, 3, 4) = 7.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program NestedViaProcVar;

{$X+}

type
  IntOp = function(a, b: Integer): Integer;
  ApplierT = function(op: IntOp; x, y: Integer): Integer;

function AddI(a, b: Integer): Integer;
begin
  AddI := a + b;
end;

function ApplyOp(op: IntOp; x, y: Integer): Integer;
begin
  ApplyOp := op(x, y);
end;

var
  MyApplier: ApplierT;

begin
  MyApplier := ApplyOp;
  Writeln(MyApplier(AddI, 3, 4));
end.
