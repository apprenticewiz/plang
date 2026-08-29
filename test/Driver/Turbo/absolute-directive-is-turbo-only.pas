(*
'absolute' is Turbo's own directive, gated in the parser on Opts.turbo()
(parseVarGroup, ParseDecl.cpp) and, deliberately, not a reserved word at all
-- it is recognized only by its spelling, only right after a
var-declaration's type (see VarGroup::AbsoluteExpr's own comment, AstDecl.h,
for why: so a program that happens to declare its own identifier called
'absolute' elsewhere is unaffected).  Under any other dialect that check
never fires, so the parser expects ';' right where 'absolute' sits and the
attempt becomes an ordinary syntax error, the same way Turbo-only typed-
constant syntax does (typed-constant-syntax-is-turbo-only.pas) -- no
dedicated dialect diagnostic needed.  Also confirms the same source accepts
and runs under -std=turbo.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: error: expected ';', got identifier 'absolute'
*)

(*
RUNS:65
*)

program p;
var
  B: array[0..1] of Char;
  W: Integer absolute B;
begin
  B[0] := Chr(65);
  B[1] := Chr(0);
  writeln(W);
end.
