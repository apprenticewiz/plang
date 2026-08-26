(*
Companion to the -std=iso7185 rejection test: the identical construct
must still compile cleanly under -std=iso10206. Fanned out from
StandardGate.AndEveryOneOfThemStillCompilesUnderIso10206's table-driven
loop -- one file per construct, 'succ(x, n)'.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
*)

program p;
begin writeln(succ(1, 2)) end.
