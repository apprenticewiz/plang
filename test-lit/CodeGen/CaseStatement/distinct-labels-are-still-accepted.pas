(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:mid
*)

program p(output);
var i: integer;
begin i := 4;
  case i of 1..3: writeln('low'); 4..6: writeln('mid') end
end.
