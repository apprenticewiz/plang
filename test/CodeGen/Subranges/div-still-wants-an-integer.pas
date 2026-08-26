(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: requires integer operands
*)

program p(output);
var r: real;
begin r := 1.0; writeln(r div 2) end.
