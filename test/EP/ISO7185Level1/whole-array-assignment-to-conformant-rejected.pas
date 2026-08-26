(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: conformant array
*)

program p;
var big: array [1..8] of integer; small: array [1..3] of integer;
procedure q(var x: array [lo..hi: integer] of integer);
begin x := big end;
begin q(small) end.
