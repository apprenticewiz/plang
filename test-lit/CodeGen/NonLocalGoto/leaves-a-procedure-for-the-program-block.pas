(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:iter 1
CHECK-NEXT:iter 2
CHECK-NEXT:landed
*)

program p(output);
label 9999;
var i: integer;
procedure bail;
begin goto 9999 end;
begin
  for i := 1 to 3 do begin
    writeln('iter ', i:1);
    if i = 2 then bail
  end;
  writeln('not reached');
9999:
  writeln('landed')
end.
