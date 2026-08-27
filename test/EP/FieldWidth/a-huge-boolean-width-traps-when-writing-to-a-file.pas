(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: write field width 5000000000 is too large
*)

program p;
var f: text; b: boolean; w: integer;
begin
  rewrite(f);
  b := true;
  w := 5000000000;
  write(f, b:w)
end.
