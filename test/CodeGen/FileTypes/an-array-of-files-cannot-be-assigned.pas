(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: file
*)

program p(output);
type fa = array [1..2] of text;
var a, b: fa;
begin a := b end.
