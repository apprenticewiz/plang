(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[built 42 here] len=13
CHECK-NEXT:copied [built 42 here]
*)

program p(output);
type ps = ^string;
var q: ps; s: string(30);
begin
  new(q, 25);
  writestr(q^, 'built ', 42:1, ' here');
  writeln('[', q^, '] len=', length(q^):1);
  s := q^;
  writeln('copied [', s, ']');
  dispose(q)
end.
