(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:done
*)

program p(output);
var i: integer;
procedure helper;
var i: integer;
begin i := 99 end;
begin
  for i := 1 to 3 do helper;
  writeln('done')
end.
