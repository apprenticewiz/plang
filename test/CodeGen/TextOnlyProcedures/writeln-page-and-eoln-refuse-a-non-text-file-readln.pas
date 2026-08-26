(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'readln'
*)

program p(output);
var f: file of integer; x: integer;
begin reset(f); readln(f, x) end.
