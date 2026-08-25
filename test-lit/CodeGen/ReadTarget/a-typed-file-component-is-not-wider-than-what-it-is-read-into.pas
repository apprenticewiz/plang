(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:qZZZ
*)

program p(output);
type letter = 'a'..'z';
var f: file of letter; s: array[1..4] of char; i: integer;
begin
  for i := 1 to 4 do s[i] := 'Z';
  rewrite(f, 'rt.bin'); write(f, 'q'); close(f);
  reset(f, 'rt.bin'); read(f, s[1]); close(f);
  for i := 1 to 4 do write(s[i]);
  writeln
end.
