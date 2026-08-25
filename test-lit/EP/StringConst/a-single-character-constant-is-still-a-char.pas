(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[x] 120
*)

program p;
const c = 'x';
var ch: char;
begin ch := c; writeln('[', ch, '] ', ord(ch)) end.
