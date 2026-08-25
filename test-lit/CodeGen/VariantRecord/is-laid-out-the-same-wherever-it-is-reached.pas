(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 9
CHECK-NEXT:3.25
*)

program p(output);
type w = record case s: integer of 1: (a: integer); 2: (b: real) end;
     pw = ^w;
var arr: array[1..3] of w; q: pw; i: integer;
begin
  for i := 1 to 3 do begin arr[i].s := 1; arr[i].a := i * i end;
  writeln(arr[1].a, ' ', arr[3].a);
  new(q); q^.s := 2; q^.b := 3.25; writeln(q^.b:0:2); dispose(q)
end.
