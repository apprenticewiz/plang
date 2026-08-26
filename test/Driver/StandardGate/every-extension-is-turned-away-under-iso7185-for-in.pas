(*
Each Extended Pascal construct is checked in a program that uses nothing
else standard Pascal lacks, so the diagnostic under -std=iso7185 is about
the construct named here and not about something else in the way. Fanned
out from StandardGate.EveryExtensionIsTurnedAwayUnderIso7185's table-
driven loop -- one file per construct, 'for ... in'.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Extended Pascal extension
*)

program p; var s: set of 1..5; i: integer;
begin s := [1, 3]; for i in s do write(i); writeln end.
