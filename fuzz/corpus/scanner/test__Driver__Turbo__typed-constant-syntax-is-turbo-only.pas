(*
Typed constants (`const X: Integer = 0;`) are Turbo's own syntax, gated in
the parser on Opts.turbo() (parseConstDef, ParseDecl.cpp): under any other
dialect the ':' the typed form needs is never tried, so parseConstDef falls
straight through to the classic 'identifier = expr' form, and a ':' where
'=' was wanted becomes an ordinary syntax error -- no dedicated dialect
diagnostic was written for this, the same way Extended Pascal's own
'value'-clause and structured-value-constructor syntax need none: attempting
Turbo-only syntax under a dialect that does not have it simply fails to
parse as anything else.  Also confirms the same source accepts and runs
under -std=turbo, so the two RUN lines below are testing the SAME construct,
not two unrelated ones.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: error: expected '=', got ':'
*)

(*
RUNS:5
*)

program p;
const X: Integer = 5;
begin
  writeln(X);
end.
