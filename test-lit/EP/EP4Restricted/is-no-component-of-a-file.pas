(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: component type of a file
*)

program p(output);
type k = restricted integer;
var f: file of k;
begin end.
