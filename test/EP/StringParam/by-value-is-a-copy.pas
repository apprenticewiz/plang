(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[changed]
CHECK-NEXT:[orig]
*)

program p;
var s: string(20);
procedure show(x: string(20));
begin x := 'changed'; writeln('[', x, ']') end;
begin s := 'orig'; show(s); writeln('[', s, ']') end.
