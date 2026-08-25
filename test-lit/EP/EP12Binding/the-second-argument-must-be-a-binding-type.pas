(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: BindingType
*)

program p;
var f: bindable text; n: integer;
begin bind(f, n) end.
