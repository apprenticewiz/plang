(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: file type
*)

program p(output);
var f, g: text;
begin f := g end.
