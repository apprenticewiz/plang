(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:qz
*)

program p(output);
var c: char; s: set of char;
procedure show; begin write(c) end;
begin s := ['q','z']; for c in s do show; writeln end.
