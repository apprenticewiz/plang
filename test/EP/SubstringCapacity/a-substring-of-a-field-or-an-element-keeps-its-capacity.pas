(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:300 300 300
*)

program p(output);
type rec = record s: string(300) end;
var r: rec; a: array[1..2] of string(300); n: string(300); k: integer;
begin
  n := '';
  for k := 1 to 300 do n := n + 'x';
  a[1] := n; r.s := n;
  writeln(length(n[1..300]), ' ', length(a[1][1..300]), ' ',
          length(r.s[1..300]))
end.
