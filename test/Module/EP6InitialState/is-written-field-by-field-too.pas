(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2.5 3
*)

program p(output);
type flags = record a: integer value 1; b: real value 2.5;
                    c: integer end;
var f: flags;
begin f.c := 3; writeln(f.a, ' ', f.b:0:1, ' ', f.c) end.
