(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot mix set and non-set
*)

program p;
var s: set of 1..10; i: integer;
begin s := [1]; i := 3; i := s + i end.
