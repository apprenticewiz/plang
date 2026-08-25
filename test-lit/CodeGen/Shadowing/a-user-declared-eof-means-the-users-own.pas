(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:user function won
*)

program p(output);
function eof: boolean;
begin eof := false end;
begin
  if eof then writeln('builtin won') else writeln('user function won')
end.
