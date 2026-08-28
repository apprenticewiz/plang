(*
Exit inside a loop's body returns from the enclosing FUNCTION, not merely
from the loop the way Break does -- it branches to CGFunction::ExitBB, an
entirely different target than the loop's own break/continue blocks
(CGControlFlow's PushLoopTargets/PopLoopTargets).  FindFirstOver3 stops as
soon as it finds an element greater than 3 and never reaches the loop's own
"not found" tail below it.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out
RUN: FileCheck %s < %t.out
*)

(*
CHECK: checked 1
CHECK-NEXT: checked 2
CHECK-NEXT: checked 3
CHECK-NEXT: checked 4
CHECK-NEXT: found=4
*)

program exit_from_loop;

function FindFirstOver3: Integer;
var
  a: array[1..5] of Integer;
  i: Integer;
begin
  a[1] := 1; a[2] := 2; a[3] := 3; a[4] := 4; a[5] := 5;
  for i := 1 to 5 do begin
    writeln('checked ', a[i]);
    if a[i] > 3 then Exit(a[i])
  end;
  FindFirstOver3 := -1
end;

var found: Integer;
begin
  found := FindFirstOver3;
  writeln('found=', found)
end.
