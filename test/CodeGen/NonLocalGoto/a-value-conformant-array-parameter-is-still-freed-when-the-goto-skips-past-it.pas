(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9999 1
CHECK-NEXT:landed
*)

program p(output);
label 1;
type big = array[1..200000] of integer;
var a: big; i: integer;

procedure clobber(x: array[lo..hi: integer] of integer);
begin
  x[lo] := 9999;
  writeln(x[lo], ' ', a[1]);
  goto 1
end;

begin
  for i := 1 to 200000 do a[i] := i;
  clobber(a);
  writeln('not reached');
1:
  writeln('landed')
end.
