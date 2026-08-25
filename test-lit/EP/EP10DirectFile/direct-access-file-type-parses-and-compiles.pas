(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ok
*)

program p;
type IntFile = file [1..10] of integer;
var f: IntFile;
    g: file [0..99] of integer;
begin
  writeln('ok')
end.
