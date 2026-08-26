(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: substr
*)

program p(output);
var s: string(10);
begin s := 'abcdefghij'; s[5..4] := ''; writeln('unreachable') end.
