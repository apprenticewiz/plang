(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:scarlet
CHECK-NEXT:emerald
CHECK-NEXT:cobalt
CHECK-NEXT:111 222
*)

program p(output);
type colour = (red, green, blue);
     t(n: integer) = record
       lo: integer; a: array[colour] of string(n); hi: integer end;
var q: ^t; c: colour;
begin new(q, 12); q^.lo := 111; q^.hi := 222;
      q^.a[red] := 'scarlet'; q^.a[green] := 'emerald';
      q^.a[blue] := 'cobalt';
      for c := red to blue do writeln(q^.a[c]);
      writeln(q^.lo:1, ' ', q^.hi:1) end.
