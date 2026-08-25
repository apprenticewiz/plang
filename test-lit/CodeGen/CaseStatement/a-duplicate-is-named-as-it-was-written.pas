(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: case label 'a'
*)

program p(output);
var c: char;
begin case c of 'a': writeln(1); 'a': writeln(2) end end.
