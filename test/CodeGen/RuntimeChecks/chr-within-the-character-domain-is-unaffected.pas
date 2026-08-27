(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 255 65
*)

(*
issue #166: chr's range check must be inclusive of both domain endpoints --
chr(0) and chr(255) are the lowest and highest values a character actually
has, so neither should trip the new guard.
*)

program p;
begin writeln(ord(chr(0)), ' ', ord(chr(255)), ' ', ord(chr(65))) end.
