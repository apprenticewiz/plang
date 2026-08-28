(*
EP §6.8.3.3: and_then/or_else always short-circuit, unconditionally --
unlike plain `and`/`or`, which stays fully-evaluating in Extended Pascal
regardless of any switch (see iso7185-and-extended-pascal-always-
evaluate-both-boolean-operands.pas, Driver/Turbo).  and-then-produces-phi
.pas and or-else-produces-phi.pas (CodeGen/IRCodeGen) already check that
the IR shape is a PHI rather than a plain `and`/`or` instruction; this is
their runtime-observable counterpart, added because CGBinaryOps'
and_then/or_else lowering (the two-block-plus-PHI CFG) was factored out
into emitShortCircuit and reused by Turbo's own dollar-B-minus and/or -- a
regression here would mean that refactor broke the ONE thing and_then/
or_else existed to guarantee.  A side effect that must never run proves
it: `false and_then X` and `true or_else X` can never depend on X's
value, so a correct short-circuit must never evaluate it.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:and_then b=false
CHECK-NEXT:or_else b=true
*)

program p(output);
var b: boolean;

function SideEffect: boolean;
begin
  writeln('SIDEEFFECT CALLED');
  SideEffect := true
end;

begin
  b := false and_then SideEffect;
  writeln('and_then b=', b);
  b := true or_else SideEffect;
  writeln('or_else b=', b)
end.
