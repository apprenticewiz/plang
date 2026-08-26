(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:01234
*)

program p;
var a: array['a'..'e'] of integer; c: char;
begin
  for c := 'a' to 'e' do a[c] := ord(c) - ord('a');
  for c := 'a' to 'e' do write(a[c]);
  writeln
end.
