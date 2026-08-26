(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 e
*)

program p(output);
type t(n: integer) = record s: string(n); d: array[1..5] of integer end;
var q: ^t; i: integer;
begin
  new(q, 8);
  with q^ do begin
    s := 'eight ch';
    for i := 1 to 5 do d[i] := i;
    writeln(d[3]:1, ' ', s[1])
  end;
  dispose(q)
end.
