(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: substring of length 2
*)

program p(output);
var s: string(10);
begin s := 'abcdef'; s[2..3] := 'TOOLONG'; writeln(s) end.
