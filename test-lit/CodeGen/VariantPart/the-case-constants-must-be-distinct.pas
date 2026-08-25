(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: more than one variant
*)

program p(output);
type r = record case t: integer of
            1: (a: integer);
            1: (b: char)
         end;
var v: r;
begin end.
