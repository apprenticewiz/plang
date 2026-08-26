(*
Everything here is emitted as arithmetic over values rather than as
constants in a type, and two of the pieces -- a dynamic alloca and the
stacksave/stackrestore pair around it -- are exactly the shapes an
optimizer is entitled to move. The suite compiles at the default level
only, so nothing else in it would notice.
*)

(*
RUN: %plang -std=iso10206 -O0 -frange-checks %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso10206 -O1 -frange-checks %s -o %t.O1
RUN: %run %t.O1 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso10206 -O2 -frange-checks %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso10206 -O3 -frange-checks %s -o %t.O3
RUN: %run %t.O3 | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[K]111x 300 300
*)

program p(output);
type t(n: integer) = record lead: integer; s: string(n);
       case tag: boolean of
         true:  (c: char;
                 case inner: boolean of
                    true: (d: real); false: (k: char));
         false: (z: integer) end;
var q: ^t; v: t(10); r: ^string; i: integer;
begin new(q, 10); q^.lead := 111; q^.s := 'ten chars!';
      q^.tag := true; q^.c := 'x';
      q^.inner := false; q^.k := 'K';
      v := q^;
      new(r, 300); r^ := '';
      for i := 1 to 300 do r^ := r^ + 'x';
      writeln('[', v.k, ']', v.lead:1, v.c, ' ',
              length(r^):1, ' ', length(trim(r^)):1) end.
