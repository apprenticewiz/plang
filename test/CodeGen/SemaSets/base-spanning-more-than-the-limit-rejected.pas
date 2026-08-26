(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: exceeds the 256-element limit
*)

program p;
var s: set of -300..0;
begin s := [] end.
