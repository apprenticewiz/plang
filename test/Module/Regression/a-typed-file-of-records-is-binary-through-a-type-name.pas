(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 4
CHECK-NEXT:5 6
*)

program p(output);
type pt = record x, y: integer end;
     ptfile = file of pt;
var f: ptfile; v: pt;
begin
  rewrite(f);
  v.x := 3; v.y := 4; write(f, v);
  v.x := 5; v.y := 6; write(f, v);
  reset(f);
  while not eof(f) do begin read(f, v); writeln(v.x, ' ', v.y) end
end.
