(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9 five! 3 6 9 12 15 / 111 222
*)

program p(output);
type t(n: integer) = record
       lead: integer;
       inner: record x: integer; a: array[1..n] of integer;
                     s: string(n) end;
       tail: integer end;
var q: ^t; i: integer;
begin new(q, 5); q^.lead := 111; q^.tail := 222;
      with q^.inner do begin
        x := 9; s := 'five!';
        for i := 1 to 5 do a[i] := i * 3 end;
      write(q^.inner.x:1, ' ', q^.inner.s, ' ');
      for i := 1 to 5 do write(q^.inner.a[i]:1, ' ');
      writeln('/ ', q^.lead:1, ' ', q^.tail:1) end.
