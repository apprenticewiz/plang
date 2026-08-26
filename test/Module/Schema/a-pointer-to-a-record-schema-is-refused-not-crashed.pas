(*
q^[1] for a ^string is a string component, EP section 6.5.3.2 -- but the
schema-array branch claimed any schema before the string case was
reached, went looking for an array body on the string schema, and killed
the compiler. A record-bodied schema has no subscript at all and has to
reach a diagnostic rather than the same crash.
*)

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: non-array
ERR-ABSENT-NOT: internal error
*)

program p(output); type buf(n: integer) = record k: integer end;
var p2: ^buf;
begin new(p2, 3); writeln(p2^[1]) end.
