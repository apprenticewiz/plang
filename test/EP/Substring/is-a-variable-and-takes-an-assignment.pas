(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:aXYdef 6
*)

program p(output);
var s: string(10);
begin s := 'abcdef'; s[2..3] := 'XY';
  writeln(s, ' ', length(s)) end.
