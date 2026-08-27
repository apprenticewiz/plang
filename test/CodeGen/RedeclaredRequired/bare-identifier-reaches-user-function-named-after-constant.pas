(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* ISO 10206 6.2.2.10 lets a program redeclare a required constant's name
   (pi, maxint, ...) as a FUNCTION; the redeclaration then denotes what the
   program wrote wherever the name is used, including a bare-identifier
   reference (an implicit niladic call, EP 6.8.2.2) with no parentheses --
   not only an explicit call. *)

(*
CHECK: 0.0000000000000000e+000
CHECK: 0.0000000000000000e+000
CHECK:0
CHECK:0
*)

program p(output);
function pi: real;
begin pi := 0.0 end;
function maxint: integer;
begin maxint := 0 end;
begin
  writeln(pi);
  writeln(pi());
  writeln(maxint);
  writeln(maxint());
end.
