(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: substring operator
*)

program p(output);
var s: packed array[1..6] of char;
begin s := 'abcdef'; s[2..3] := 'XY'; writeln(s) end.
