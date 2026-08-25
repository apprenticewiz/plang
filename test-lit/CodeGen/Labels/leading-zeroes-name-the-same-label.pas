(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:reached
*)

program p(output);
label 003;
begin
  goto 3;
  writeln('skipped');
  03: writeln('reached')
end.
