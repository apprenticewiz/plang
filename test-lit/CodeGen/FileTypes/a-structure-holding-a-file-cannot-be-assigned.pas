(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: file
*)

program p(output);
type holder = record f: text; n: integer end;
var a, b: holder;
begin a := b end.
