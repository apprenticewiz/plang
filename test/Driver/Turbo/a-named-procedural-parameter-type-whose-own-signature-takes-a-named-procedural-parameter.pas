(*
Issue #543's own general-case stress test, beyond the two literal repros:
the bug (CodeGenProcs.cpp's paramMeta_ population missing a NAMED-type
procedural parameter) has three siblings inside ClosureAndCallABI.cpp
(procParamFnType/procVarFnType/flattenProcParams/emitProcVarCall) that
independently re-detect whether ONE OF A PROCEDURAL TYPE'S OWN PARAMETERS
is itself procedural, so that its {entry point, frame} pair is built and
relayed correctly. Each used the same un-alias-resolved dyn_cast and would
still miscompile this program even after CodeGenProcs.cpp's own fix alone:
IntOpApplier's declared parameter 'op' is itself of a NAMED procedural type
(IntOp), nested inside another named procedural parameter type
(IntOpApplier) -- ApplyTwice receives 'applier' (of type IntOpApplier) and
relays 'op' through it on every call.

ApplyOnce(Double, 3) = 6; ApplyOnce(Double, 6) = 12.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:12
*)

program NestedNamedProcParam;
type
  IntOp = function(a: Integer): Integer;
  IntOpApplier = function(op: IntOp; x: Integer): Integer;

function Double(a: Integer): Integer;
begin
  Double := a * 2;
end;

function ApplyOnce(op: IntOp; x: Integer): Integer;
begin
  ApplyOnce := op(x);
end;

function ApplyTwice(applier: IntOpApplier; op: IntOp; x: Integer): Integer;
begin
  ApplyTwice := applier(op, applier(op, x));
end;

begin
  Writeln(ApplyTwice(ApplyOnce, Double, 3));
end.
