(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not declared bindable
*)

program p;
var f: text; b: BindingType;
begin b.name := '/tmp/plang_notbindable.txt'; bind(f, b) end.
