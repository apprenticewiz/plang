(*
Under Turbo, a case-statement with no else/otherwise part falls through
instead of trapping when the selector matches no arm (checkCase's own
Opts.turbo() gate just above its exhaustiveness warning, SemaStmt.cpp;
CGControlFlow::emitCase lowers it the same way) -- ISO/EP's "a selector
matching no arm is an error" (§6.8.3.5) is exactly the premise
checkDefiniteAssignment's CaseStmt arm (SemaFlow.cpp) used to reason "every
path past the case went through an arm" from, and that reasoning is false
here: i might not be 1, in which case the case-statement does nothing and x
is never touched.  Regression test for the fix: this used to compile clean
(see iso-case-with-no-else-still-traps-so-the-arms-alone-settle-it.pas for
the dialects where that silence is still correct).
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'x' is read here before it has been given a value
*)

program p;
var i, x: Integer;
begin
  i := 1;
  case i of
    1: x := 1
  end;
  writeln(x)
end.
