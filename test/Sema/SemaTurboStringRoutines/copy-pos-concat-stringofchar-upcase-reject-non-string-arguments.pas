(*
System-unit string routines item: Copy/Pos/Concat/StringOfChar/UpCase each
check their string-shaped argument(s) the same way EP's length/substr/trim
already do (err_string_fn_arg_type, shared with those) -- see
checkCallExpr's own isTurboStringLike lambda (SemaExpr.cpp).  StringOfChar's
Ch and UpCase's own argument are Char-only, so a string argument there is
ALSO rejected -- checked with err_string_fn_arg_type too, the message it
gives being equally true of a String argument ("it must be char or
string").

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'integer' cannot be an argument of copy; it must be char or string
CHECK: 'integer' cannot be an argument of pos; it must be char or string
CHECK: 'integer' cannot be an argument of pos; it must be char or string
CHECK: 'integer' cannot be an argument of concat; it must be char or string
CHECK: 'integer' cannot be an argument of stringofchar; it must be char or string
CHECK: 'integer' cannot be an argument of upcase; it must be char or string
*)

program p;
var
  i, n: integer;
  s: string;
  c: char;
begin
  i := 42;
  s := copy(i, 1, 2);
  n := pos(i, s);
  n := pos(s, i);
  s := concat(i);
  s := stringofchar(i, 3);
  c := upcase(i);
end.
