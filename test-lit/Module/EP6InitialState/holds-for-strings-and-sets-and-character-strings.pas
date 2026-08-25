(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi2
CHECK-NEXT:true false true
CHECK-NEXT:abc
*)

program p(output);
type name   = string(10) value 'hi';
     digits = set of 0..9 value [1, 3..4];
     tag    = packed array [1..3] of char value 'abc';
var s: name; d: digits; t: tag;
begin writeln(s, length(s));
  writeln(1 in d, ' ', 2 in d, ' ', 4 in d);
  writeln(t) end.
