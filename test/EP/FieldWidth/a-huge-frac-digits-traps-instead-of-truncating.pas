(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: write field width 5000000000 is too large
*)

program p;
var r: real; d: integer;
begin
  r := 3.14;
  d := 5000000000;
  write(r:10:d)
end.
