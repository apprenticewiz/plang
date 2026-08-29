(*
System-unit string routines item: Delete/SetLength's own string argument,
Insert's SECOND (destination) argument, Str's destination and Val's v/code
are all var parameters and need a real address -- checkCallStmt's own arms
reuse err_var_param_needs_lvalue, the same diagnostic FillChar/Move's
"untyped" arguments already give a non-variable actual (see
SemaTurboFillCharMove/fillchar-and-move-arguments-must-be-variables.pas).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: argument 1 of 'delete' is a 'var' parameter and requires a variable
CHECK: argument 2 of 'insert' is a 'var' parameter and requires a variable
CHECK: argument 1 of 'setlength' is a 'var' parameter and requires a variable
CHECK: argument 2 of 'str' is a 'var' parameter and requires a variable
CHECK: argument 2 of 'val' is a 'var' parameter and requires a variable
CHECK: argument 3 of 'val' is a 'var' parameter and requires a variable
*)

program p;
var
  s: string;
  i, n: integer;
begin
  s := 'hello';
  Delete(s + '', 1, 2);
  Insert('x', s + '', 1);
  SetLength(s + '', 3);
  Str(i, s + '');
  Val(s, i + 1, n);
  Val(s, i, n + 1);
end.
