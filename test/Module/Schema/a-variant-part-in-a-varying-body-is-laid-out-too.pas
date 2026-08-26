(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:d=abcdef tag=true x=1234
*)

program p(output);
type buf(n: integer) = record d: array[1..n] of char;
       case tag: boolean of true: (x: integer); false: (y: real) end;
var q: ^buf; i: integer;
begin
  new(q, 6);
  for i := 1 to 6 do q^.d[i] := chr(ord('a') + i - 1);
  q^.tag := true; q^.x := 1234;
  write('d='); for i := 1 to 6 do write(q^.d[i]);
  writeln(' tag=', q^.tag, ' x=', q^.x:1);
  dispose(q)
end.
