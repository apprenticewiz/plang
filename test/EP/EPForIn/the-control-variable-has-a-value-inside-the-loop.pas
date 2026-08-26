(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t > %t.out 2> %t.err
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
CHECK:abc
ERR-ABSENT-NOT: before it has been given a value
*)

program p(output);
var c: char;
begin for c in ['a'..'c'] do write(c); writeln end.
