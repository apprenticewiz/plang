(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:other
*)

program p(output);
var i: integer;
begin i := 5;
  case i of
    1: writeln('one')
    otherwise writeln('other')
  end
end.
