(*
System-unit string routines item: Concat(s1 [, s2, ..., sn]) -- variadic
concatenation, one or more arguments, built by chaining the SAME
plang_sstr_concat the `+` operator's own ShortString arm already calls
(CGBinaryOps.cpp), starting from an empty accumulator -- see CGFuncCall.cpp's
own comment.  Also exercises Concat with exactly one argument (the identity
case: the empty-accumulator chain still has to produce that one argument's
own value unchanged).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
begin
  writeln(Concat('foo', 'bar', 'baz'));
  writeln(Concat('solo'));
  writeln(Concat('a', '', 'b'));
end.

(*
CHECK:foobarbaz
CHECK-NEXT:solo
CHECK-NEXT:ab
*)
