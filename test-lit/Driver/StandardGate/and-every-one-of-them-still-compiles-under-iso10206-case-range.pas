(*
Companion to the -std=iso7185 rejection test: the identical construct
must still compile cleanly under -std=iso10206. Fanned out from
StandardGate.AndEveryOneOfThemStillCompilesUnderIso10206's table-driven
loop -- one file per construct, 'case range'.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
*)

program p; var i: integer;
begin i := 2; case i of 1..3: writeln('low') end end.
