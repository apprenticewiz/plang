(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot be read into
*)

program p(input, output);
var b: boolean;
begin read(b) end.
