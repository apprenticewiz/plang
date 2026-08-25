(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: set
*)

program p;
var x, v: integer;
begin for v in x do writeln(v) end.
