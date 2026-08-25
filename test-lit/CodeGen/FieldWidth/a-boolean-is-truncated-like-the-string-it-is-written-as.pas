(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  true
CHECK-NEXT: true
CHECK-NEXT:true
CHECK-NEXT:tru
CHECK-NEXT:tr
CHECK-NEXT:t
CHECK-NEXT: false
CHECK-NEXT:false
CHECK-NEXT:fals
CHECK-NEXT:fal
CHECK-NEXT:fa
CHECK-NEXT:f
*)

program p(output);
var i: integer;
begin
  for i := 6 downto 1 do writeln(true: i);
  for i := 6 downto 1 do writeln(false: i)
end.
