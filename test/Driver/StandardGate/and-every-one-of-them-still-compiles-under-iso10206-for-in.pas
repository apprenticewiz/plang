(*
Companion to the -std=iso7185 rejection test: the identical construct
must still compile cleanly under -std=iso10206. Fanned out from
StandardGate.AndEveryOneOfThemStillCompilesUnderIso10206's table-driven
loop -- one file per construct, 'for ... in'.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
*)

program p; var s: set of 1..5; i: integer;
begin s := [1, 3]; for i in s do write(i); writeln end.
