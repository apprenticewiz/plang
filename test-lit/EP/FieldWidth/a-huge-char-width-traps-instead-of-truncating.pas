(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: write field width 5000000000 is too large
*)

program p;
var c: char; w: integer;
begin
  c := 'x';
  w := 5000000000;
  write(c:w)
end.
