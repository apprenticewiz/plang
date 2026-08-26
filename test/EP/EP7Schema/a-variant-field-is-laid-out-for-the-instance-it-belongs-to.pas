(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:big 1 77 66
CHECK-NEXT:small 2 88 22
*)

program p(output);
type inner(m: integer) = record a: array[1..m] of integer end;
     outer(n: integer) = record
        k: integer;
        case tag: boolean of
          true:  (x: inner(n); y: integer);
          false: (z: integer)
     end;
var big: outer(6); small: outer(2);
begin
  big.k := 1; big.tag := true; big.y := 77; big.x.a[6] := 66;
  small.k := 2; small.tag := true; small.y := 88; small.x.a[2] := 22;
  writeln('big ', big.k:1, ' ', big.y:1, ' ', big.x.a[6]:1);
  writeln('small ', small.k:1, ' ', small.y:1, ' ', small.x.a[2]:1)
end.
