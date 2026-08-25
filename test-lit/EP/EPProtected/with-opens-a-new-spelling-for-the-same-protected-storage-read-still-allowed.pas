(*
`with r do` rebinds each field to a fresh with-scope symbol, so
checkNotProtected -- looking up whatever identifier was actually
written -- found that symbol instead of r's, and it was never marked
protected.  `with r do f := 5` silently wrote through a `protected var`
parameter with no diagnostic at all.
*)

(* A read through the same `with` must still be allowed. *)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
type rec = record f: integer end;
var g: rec;
procedure q3(protected var r: rec);
begin with r do writeln(f:1) end;
begin g.f := 42; q3(g) end.
