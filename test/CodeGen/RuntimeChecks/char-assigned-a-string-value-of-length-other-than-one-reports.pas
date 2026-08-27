(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: a string of length 2 cannot fill a 1-character string-type
*)

(* issue #231: EP §6.4.6(f) makes a string value assignment-compatible with a
   char variable only when its length is exactly 1 -- char's own capacity,
   per §6.4.3.3.1.  s's declared capacity is 5, but its run-time length after
   `s := 'xy'` is 2; codegen has to check the length the value actually has,
   since a string(n)'s capacity says nothing about it. *)
program p;
var s: string(5); c: char;
begin
  s := 'xy';
  c := s;
  writeln('should not get here')
end.
