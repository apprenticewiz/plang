(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:101 102 103
*)

program p(output);
type vec = array[1..3] of integer;
var a: vec; i: integer;
procedure noise(z: integer); begin end;
procedure viaproc(procedure f(z: integer);
                  var x: array[lo..hi: integer] of integer;
                  tail: integer);
var k: integer;
begin for k := lo to hi do x[k] := 100 + k end;
begin
  for i := 1 to 3 do a[i] := i;
  viaproc(noise, a, 0);
  write(a[1], ' ', a[2], ' ', a[3]); writeln
end.
