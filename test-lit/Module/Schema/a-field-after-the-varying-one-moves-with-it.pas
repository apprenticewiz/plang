(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcd] 11 len=4
CHECK-NEXT:[a much longer string] 22 len=20
*)

program p(output);
type buf(cap: integer) = record s: string(cap); n: integer end;
     pb = ^buf;
var a, b: pb;
begin
  new(a, 4);  a^.s := 'abcd';          a^.n := 11;
  new(b, 40); b^.s := 'a much longer string'; b^.n := 22;
  writeln('[', a^.s, '] ', a^.n:1, ' len=', length(a^.s):1);
  writeln('[', b^.s, '] ', b^.n:1, ' len=', length(b^.s):1);
  dispose(a); dispose(b)
end.
