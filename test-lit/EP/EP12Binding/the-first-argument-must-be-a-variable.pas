(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: must be a variable
*)

program p;
var b: BindingType;
begin bind(42, b) end.
