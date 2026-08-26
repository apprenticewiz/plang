(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:z 2.5 4321
*)

program p(output);
type buf(n: integer) = record d: array[1..n] of char;
       case tag: boolean of true: (x: char); false: (y: real) end;
var q: ^buf; canary: integer;
begin
  canary := 4321;
  new(q, 3); q^.d[1] := 'z';
  q^.tag := false; q^.y := 2.5;
  writeln(q^.d[1], ' ', q^.y:3:1, ' ', canary:1);
  dispose(q)
end.
