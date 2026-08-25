(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: string index 0
*)

program p(output); var s: string(10);
begin s := 'ab'; writeln(s[0]) end.
