(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a100 b200 c300 
*)

program p(output);
type rec(n: integer) = array[1..n] of record
       c: char;
       case tag: boolean of true: (i: integer); false: (j: integer)
     end;
var q: ^rec; k: integer;
begin
  new(q, 3);
  for k := 1 to 3 do begin
    q^[k].c := chr(96+k); q^[k].tag := true; q^[k].i := k*100 end;
  for k := 1 to 3 do write(q^[k].c, q^[k].i:1, ' ');
  writeln; dispose(q)
end.
