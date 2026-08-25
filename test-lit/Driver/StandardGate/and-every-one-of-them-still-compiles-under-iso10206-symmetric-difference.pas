(*
Companion to the -std=iso7185 rejection test: the identical construct
must still compile cleanly under -std=iso10206. Fanned out from
StandardGate.AndEveryOneOfThemStillCompilesUnderIso10206's table-driven
loop -- one file per construct, '><'.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
*)

program p; var a, b: set of 1..5;
begin a := [1, 2]; b := [2, 3]; a := a >< b; writeln(1 in a) end.
