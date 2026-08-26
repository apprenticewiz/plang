(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: simple or pointer
*)

program p(output);
type arr = array [1..3] of integer;
var v: arr;
function q: arr; begin q := v end;
begin end.
