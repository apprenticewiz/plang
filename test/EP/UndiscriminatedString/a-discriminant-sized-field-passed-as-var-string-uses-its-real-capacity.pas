(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:hello
*)

program p(output);
type t(n: integer) = record s: string(n); k: integer end;
var q: ^t;
procedure work(var s: string);
begin
  writeln(length(s):1);
  s := 'hello'
end;
begin
  new(q, 10);
  q^.s := 'hi';
  work(q^.s);
  writeln(q^.s)
end.
