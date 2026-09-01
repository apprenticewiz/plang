(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not fit a string(5)
*)

program p;
const maxlen = 5;
var s: string(maxlen);
begin s := 'abcdefghij' end.
