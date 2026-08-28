(*
The critical non-regression check for this whole feature: ISO §6.7.2.1
requires `and`/`or` to evaluate BOTH operands, always -- not "the old
behavior before short-circuiting was added", a real requirement neither
ISO 7185 nor Extended Pascal ever relaxes.  Neither dialect has a dollar-B
directive to turn that off (BoolEval, CompilerSwitches.def, is recorded
only when Opts.turbo() -- see LangOptions::Switches's own comment), so
CGBinaryOps::emitBinary's And/Or arm must gate its short-circuit dispatch
on isTurbo() before it ever asks RangeGuards.boolEvalAt, rather than
trusting that query's own SwitchTable default (which answers
"short-circuit" for every dialect alike).  Proven here the same way the
Turbo short-circuit tests prove their own case: an observable side effect
on BOTH operands, of an `and` whose LEFT operand alone already decides the
result (false and anything is false) and an `or` whose LEFT operand alone
already decides ITS result (true or anything is true) -- under a correct
full-evaluation dialect, the right operand's side effect still runs
regardless.  Same source under both dialects; only -std changes.
*)

(*
RUN: %plang -std=iso7185 %s -o %t.iso
RUN: %run %t.iso | FileCheck --strict-whitespace --match-full-lines %s

RUN: %plang -std=iso10206 %s -o %t.ep
RUN: %run %t.ep | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:LEFT CALLED
CHECK-NEXT:RIGHT CALLED
CHECK-NEXT:and b=false
CHECK-NEXT:RIGHT CALLED
CHECK-NEXT:LEFT CALLED
CHECK-NEXT:or b=true
*)

program p(output);
var b: boolean;

function SideEffectL: boolean;
begin
  writeln('LEFT CALLED');
  SideEffectL := false
end;

function SideEffectR: boolean;
begin
  writeln('RIGHT CALLED');
  SideEffectR := true
end;

begin
  b := SideEffectL and SideEffectR;
  writeln('and b=', b);
  b := SideEffectR or SideEffectL;
  writeln('or b=', b)
end.
