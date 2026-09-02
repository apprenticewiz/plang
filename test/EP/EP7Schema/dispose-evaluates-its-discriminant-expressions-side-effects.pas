(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:called
CHECK-NEXT:done
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var q: ^vec;
function sideEffect: integer;
begin writeln('called'); sideEffect := 5 end;
begin
  new(q, 5);
  dispose(q, sideEffect());
  writeln('done')
end.
