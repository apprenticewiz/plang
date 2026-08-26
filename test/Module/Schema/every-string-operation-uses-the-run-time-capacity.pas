(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:len=11
CHECK-NEXT:sub=[hello]
CHECK-NEXT:substr=[world]
CHECK-NEXT:cat=[hello world!]
CHECK-NEXT:eq=true idx=7
CHECK-NEXT:after=[HELLO world]
*)

program p(output);
type ps = ^string;
var q, r: ps;
begin
  new(q, 30); new(r, 30);
  q^ := 'hello world'; r^ := 'hello world';
  writeln('len=', length(q^):1);
  writeln('sub=[', q^[1..5], ']');
  writeln('substr=[', substr(q^, 7, 5), ']');
  writeln('cat=[', q^ + '!', ']');
  writeln('eq=', q^ = r^, ' idx=', index(q^, 'world'):1);
  q^[1..5] := 'HELLO';
  writeln('after=[', q^, ']');
  dispose(q); dispose(r)
end.
