(*
Companion to the -std=iso7185 rejection test: the identical construct
must still compile cleanly under -std=iso10206. Fanned out from
StandardGate.AndEveryOneOfThemStillCompilesUnderIso10206's table-driven
loop -- one file per construct, 'complex'.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
*)

program p; var c: complex;
begin c := cmplx(1, 2); writeln(re(c):0:1) end.
