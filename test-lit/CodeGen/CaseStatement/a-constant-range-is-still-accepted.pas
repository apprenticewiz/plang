(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:in range
*)

program p(output);
const hi = 3;
var i: integer;
begin
  i := 2;
  case i of
    1..hi: writeln('in range');
    otherwise writeln('out')
  end
end.
