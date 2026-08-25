(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcpqr
*)

program p(output);
var s: string(10); t: string(3);
begin s := 'abcdef'; t := 'pq';
  s[4..6] := t + 'r';
  writeln(s) end.
