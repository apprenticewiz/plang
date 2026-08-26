(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:in [hi] len=2
CHECK-NEXT:  eq
CHECK-NEXT:  sub [hi]
CHECK-NEXT:in [hi] len=2
CHECK-NEXT:  eq
CHECK-NEXT:  sub [hi]
CHECK-NEXT:out [zz] [zz]
*)

program p(output);
procedure work(var s: string);
begin
  writeln('in [', s, '] len=', length(s):1);
  if s = 'hi' then writeln('  eq');
  writeln('  sub [', substr(s, 1, 2), ']');
  s := 'zz'
end;
var a: string(10); b: string(4);
begin
  a := 'hi'; b := 'hi';
  work(a); work(b);
  writeln('out [', a, '] [', b, ']')
end.
