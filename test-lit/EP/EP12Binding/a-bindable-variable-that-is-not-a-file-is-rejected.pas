(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: only a file variable
*)

program p;
var n: bindable integer; b: BindingType;
begin b.name := '/tmp/x'; bind(n, b) end.
