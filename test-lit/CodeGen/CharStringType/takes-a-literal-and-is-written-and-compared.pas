(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello world
CHECK-NEXT:eq
CHECK-NEXT:lt
CHECK-NEXT:ne
CHECK-NEXT:hello
*)

program p(output);
var s, t: packed array[1..5] of char; i: integer;
begin
  s := 'hello'; t := 'world';
  write(s); write(' '); writeln(t);
  if s = 'hello' then writeln('eq');
  if s < t then writeln('lt');
  if s <> t then writeln('ne');
  for i := 1 to 5 do write(s[i]);
  writeln
end.
