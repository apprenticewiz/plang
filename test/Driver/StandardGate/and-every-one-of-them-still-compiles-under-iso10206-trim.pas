(*
Companion to the -std=iso7185 rejection test: the identical construct
must still compile cleanly under -std=iso10206. Fanned out from
StandardGate.AndEveryOneOfThemStillCompilesUnderIso10206's table-driven
loop -- one file per construct, 'trim'.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
*)

program p; var s: string(10);
begin s := 'ab'; writeln(trim(s)) end.
