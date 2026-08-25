(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
var ch: char; n: integer;
begin
  n := 0;
  for ch := 'a' to 'z' do
    if ch in ['a', 'e', 'i', 'o', 'u'] then n := n + 1;
  writeln(n)
end.
