(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not fit a string(3)
*)

program p(output); var s: string(3);
begin s := 'abcdef' end.
