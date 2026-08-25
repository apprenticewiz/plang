(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of range
*)

program p(output);
type Color = (red, green, blue); var c: Color;
begin c := blue; writeln(ord(succ(c))) end.
