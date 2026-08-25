(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
CHECK-NEXT:by
CHECK-NEXT:11
*)

program p(output);
type color = (red, green, blue);
begin
  writeln(succ(false), ' ', pred(true));
  writeln(succ('a'), pred('z'));
  writeln(ord(succ(red)):1, ord(pred(blue)):1)
end.
