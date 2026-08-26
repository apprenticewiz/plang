(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a varying body / 42
CHECK-NEXT:back the other way / 99
*)

program p(output);
type t(n: integer) = record s: string(n); k: integer end;
var q: ^t; v: t(20); w: t(20);
begin new(q, 20);
      q^.s := 'a varying body'; q^.k := 42;
      v := q^;
      writeln(v.s, ' / ', v.k:1);
      w.s := 'back the other way'; w.k := 99;
      q^ := w;
      writeln(q^.s, ' / ', q^.k:1) end.
