(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5ZZZ
*)

//--- test.pas
program p(input, output);
var s: array[1..4] of char; i: integer;
begin
  for i := 1 to 4 do s[i] := 'Z';
  read(s[1]);
  for i := 1 to 4 do write(s[i]);
  writeln
end.

//--- stdin.txt
5
