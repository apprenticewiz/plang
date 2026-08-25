(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:literal   
CHECK-NEXT:Xiteral   
CHECK-NEXT:variable  
CHECK-NEXT:Xariable  
CHECK-NEXT:variable  
*)

program p(output);
type s10 = packed array [1..10] of char;
var s: s10;
procedure show(t: s10);
begin writeln(t); t[1] := 'X'; writeln(t) end;
begin
  show('literal   ');
  s := 'variable  '; show(s); writeln(s)
end.
