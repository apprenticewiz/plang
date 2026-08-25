(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcdef]
*)

program p(output); var n: string(30);
function f: string(20); begin f := 'abc' end;
begin n := f + 'def'; writeln('[', n, ']') end.
