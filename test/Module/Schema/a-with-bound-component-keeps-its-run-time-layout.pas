(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 6 9 12 15 | [five!] 9
*)

program p(output);
type t(n: integer) = record
       d: array[1..n] of integer;
       inner: record s: string(n); k: integer end
     end;
var q: ^t; i: integer;
begin
  new(q, 5);
  with q^ do begin
    for i := 1 to 5 do d[i] := i * 3;
    inner.s := 'five!'; inner.k := 9
  end;
  with q^ do begin
    for i := 1 to 5 do write(d[i]:1, ' ');
    writeln('| [', inner.s, '] ', inner.k:1)
  end;
  dispose(q)
end.
