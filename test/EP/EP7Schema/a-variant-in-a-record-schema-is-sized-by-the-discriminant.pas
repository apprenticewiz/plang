(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 51
CHECK-NEXT:104 504
*)

program p(output);
type vrec(n: integer) = record
  w: array[0..n] of integer;
  case kind: boolean of
    true:  (many: array[0..n] of integer);
    false: (one: integer)
end;
var a: vrec(1); b: vrec(4); i: integer;
begin
  a.kind := true; b.kind := true;
  for i := 0 to 1 do begin a.w[i] := i + 10; a.many[i] := i + 50 end;
  for i := 0 to 4 do begin b.w[i] := i + 100; b.many[i] := i + 500 end;
  writeln(a.w[1]:0, ' ', a.many[1]:0);
  writeln(b.w[4]:0, ' ', b.many[4]:0)
end.
