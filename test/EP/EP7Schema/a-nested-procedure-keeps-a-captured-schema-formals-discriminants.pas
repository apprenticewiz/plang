(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:relayed: [nine char]
CHECK-NEXT:a: 1 2 3 4 5 6 7 8 9 | s=[nine char]
*)

program p(output);
type t(n: integer) = record a: array[1..n] of integer; s: string(n) end;
var q: ^t;
procedure show(var w: t);
begin writeln('relayed: [', w.s, ']') end;
procedure outer(var v: t);
var i: integer;
  procedure inner;
  begin v.s := 'nine char'; show(v) end;
begin
  for i := 1 to 9 do v.a[i] := i;
  inner;
  write('a: '); for i := 1 to 9 do write(v.a[i]:1, ' ');
  writeln('| s=[', v.s, ']')
end;
begin new(q, 9); outer(q^) end.
