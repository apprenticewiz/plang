(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'page'
*)

program p(output);
var f: file of integer; x: integer;
begin rewrite(f); page(f) end.
