(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22 33 
CHECK-NEXT:555 22 33 7777
*)

program p(output);
type vec = array[1..3] of integer;
var a: vec; canary: integer;
procedure work(var x: array[lo..hi: integer] of integer);
  procedure show;
  var k: integer;
  begin for k := lo to hi do write(x[k], ' '); writeln;
    x[lo] := 555 end;
begin show end;
begin
  a[1] := 11; a[2] := 22; a[3] := 33; canary := 7777;
  work(a);
  writeln(a[1], ' ', a[2], ' ', a[3], ' ', canary)
end.
