(*
RUN: not %plang_ep %s -o %t
*)

program p;
var f: text; b: BindingType;
begin unbind(f); b := binding(f) end.
