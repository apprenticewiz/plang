(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: assigned to a string(3)
*)

program p(output); var s: string(3); u: string(10);
begin u := 'abcdef'; s := u; writeln(s) end.
