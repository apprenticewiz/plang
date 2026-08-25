(*
RUN: not %plang %s -o %t
*)

program p;
type c1 = (aa, bb, cc); c2 = (dd, ee, ff);
     s1 = aa..cc; s2 = dd..ff;
var u: s1; v: s2;
begin u := aa; v := dd; u := v end.
