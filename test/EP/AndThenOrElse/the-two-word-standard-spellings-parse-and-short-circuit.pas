(*
Issue #690: ISO 10206 §6.1.2/§6.8.3.3's own word-symbols are the TWO-WORD
'and then'/'or else' -- 'and_then'/'or_else' (this codebase's underscored
spellings, checked by short-circuit-runtime-behavior-is-unaffected-by-the
-turbo-refactor.pas beside this file) are not in the standard at all.  The
parser used to have no notion of a two-word operator, so 'and then'/
'or else' fell through to 'and'/'or' consuming just the first word, and
the bare 'then'/'else' left behind read as "expected expression, got
'then'".  Same side-effect proof as the underscored form's own test: a
value that could only be produced by evaluating the right operand proves
short-circuiting still happens for the two-word spelling too.

RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:and then b=false
CHECK-NEXT:or else b=true
*)

program p(output);
var b: boolean;

function SideEffect: boolean;
begin
  writeln('SIDEEFFECT CALLED');
  SideEffect := true
end;

begin
  b := false and then SideEffect;
  writeln('and then b=', b);
  b := true or else SideEffect;
  writeln('or else b=', b)
end.
