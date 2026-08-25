(*
q^[1] for a ^string is a string component, EP section 6.5.3.2 -- but the
schema-array branch claimed any schema before the string case was
reached, went looking for an array body on the string schema, and killed
the compiler. A record-bodied schema has no subscript at all and has to
reach a diagnostic rather than the same crash.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a
*)

program p(output); type ps = ^string; var q: ps;
begin new(q, 8); q^ := 'abc'; writeln(q^[1]) end.
