(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[K][K]111x
*)

program p(output);
type t(n: integer) = record
       lead: integer;
       s: string(n);
       case tag: boolean of
         true:  (c: char;
                 case inner: boolean of
                    true:  (d: real);
                    false: (k: char));
         false: (z: integer) end;
var q: ^t; v: t(10);
begin new(q, 10);
      q^.lead := 111; q^.s := 'ten chars!';
      q^.tag := true; q^.c := 'x';
      q^.inner := false; q^.k := 'K';
      v := q^;
      writeln('[', q^.k, ']', '[', v.k, ']', v.lead:1, v.c) end.
