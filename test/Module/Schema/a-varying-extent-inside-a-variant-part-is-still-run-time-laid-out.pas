(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 true [inside the variant] 999
*)

program p(output);
type buf(n: integer) = record
       k: integer;
       case tag: boolean of true: (s: string(n)); false: (x: integer)
     end;
var q: ^buf; canary: integer;
begin
  canary := 999;
  new(q, 20);
  q^.k := 5; q^.tag := true; q^.s := 'inside the variant';
  writeln(q^.k:1, ' ', q^.tag, ' [', q^.s, '] ', canary:1);
  dispose(q)
end.
