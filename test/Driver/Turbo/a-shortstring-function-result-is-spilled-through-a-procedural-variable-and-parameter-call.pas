(*
Issue #684: a ShortString function's result, called THROUGH a procedural
parameter or a procedural variable, used to fail LLVM IR verification.
CGCallMarshal::spillStructReturnIfNeeded -- the DIRECT-call path
(CGFuncCall::emitUserFuncCall and friends) -- already spills both EP
string(N) (varStrTypeOf) and Turbo's ShortString (shortStrTypeOf) results
to a temporary, so every consumer of a string expression gets an address
rather than the raw struct value. But ClosureAndCallABI::emitProcParamCall
and emitProcVarCall's own, separate copy of that same spill only ever
checked varStrTypeOf, never shortStrTypeOf (ClosureAndCallABI.cpp:426/530),
so calling a ShortString-returning function INDIRECTLY -- through a
procedural variable, or through a procedural parameter -- reached "Call
parameter type does not match function signature!" instead, even though
the identical direct call worked. Fixed by a shared
needsStructReturnSpill predicate (ClosureAndCallABI.h) both call sites now
consult, covering ShortString the same way the direct-call path already
did.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi
CHECK-NEXT:hi
*)

program p;

type
  TFn = function(n: Integer): String;

function Greet(n: Integer): String;
begin
  Greet := 'hi';
end;

function Apply(f: TFn; n: Integer): String;
begin
  { Calls f THROUGH a procedural PARAMETER -- emitProcParamCall. }
  Apply := f(n);
end;

var
  f: TFn;
  s: String;

begin
  f := Greet;
  s := f(2);              { calls Greet THROUGH a procedural VARIABLE -- emitProcVarCall }
  writeln(s);
  s := Apply(Greet, 3);
  writeln(s);
end.
