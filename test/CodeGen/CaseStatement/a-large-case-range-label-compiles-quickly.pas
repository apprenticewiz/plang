(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:in range
*)

program p(output);
var i: integer;
begin
  i := 42;
  case i of
    1..100000000: writeln('in range');
    otherwise writeln('out')
  end
end.
