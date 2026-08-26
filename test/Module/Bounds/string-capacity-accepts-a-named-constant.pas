(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcde]
*)

program p;
const maxlen = 5;
var s: string(maxlen);
begin
  s := 'abcde';
  writeln('[', s, ']')
end.
