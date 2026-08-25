(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[42]
CHECK-NEXT:[X]
CHECK-NEXT:[hi]
CHECK-NEXT:[true]
CHECK-NEXT:[42]
CHECK-NEXT:[hi]
*)

program p(output);
var n: integer; s: string(10); c: char; b: boolean; w: integer;
begin
  n := 42; s := 'hi'; c := 'X'; b := true; w := -1;
  write('[', n:-1, ']'); writeln;
  write('[', c:-1, ']'); writeln;
  write('[', s:-1, ']'); writeln;
  write('[', b:-1, ']'); writeln;
  write('[', n:w, ']'); writeln;
  write('[', s:w, ']'); writeln
end.
