(*
RUN: not %plang_ep %s -o %t
*)

program p;
var f: bindable text;
begin bind(f) end.
