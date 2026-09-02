(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:called
CHECK-NEXT:done
*)

program p;
type
  rec = record
    case tag: boolean of
      true: (x: integer);
      false: (y: real)
  end;
var q: ^rec;
function sideEffect: boolean;
begin writeln('called'); sideEffect := true end;
begin
  new(q, true);
  dispose(q, sideEffect());
  writeln('done')
end.
