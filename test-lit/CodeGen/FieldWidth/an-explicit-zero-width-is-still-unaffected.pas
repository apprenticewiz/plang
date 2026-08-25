(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[42]
CHECK-NEXT:[]
CHECK-NEXT:[]
CHECK-NEXT:[]
*)

program p(output);
var n: integer; s: string(10); c: char; b: boolean;
begin
  n := 42; s := 'hi'; c := 'X'; b := true;
  write('[', n:0, ']'); writeln;
  write('[', c:0, ']'); writeln;
  write('[', s:0, ']'); writeln;
  write('[', b:0, ']'); writeln
end.
