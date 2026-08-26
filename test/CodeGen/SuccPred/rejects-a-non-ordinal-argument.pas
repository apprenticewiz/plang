(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: requires an ordinal argument
*)

program p;
var r: real;
begin r := succ(1.5); writeln(r) end.
