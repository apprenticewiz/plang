(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
CHECK-NEXT:[one][two]
*)

program p;
var s: string(20);
function len2(x: string(20)): integer;
begin len2 := length(x) * 2 end;
procedure both(a: string(10); b: string(10));
begin writeln('[', a, '][', b, ']') end;
begin s := 'abc'; writeln(len2(s)); both('one', 'two') end.
