(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program p(output);
type intfile = file of integer;
var f: intfile; i: integer;
begin
  rewrite(f); write(f, 11); write(f, 22); write(f, 33);
  reset(f);
  while not eof(f) do begin read(f, i); writeln(i) end
end.
