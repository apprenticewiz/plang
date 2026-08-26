(*
`with r do` rebinds each field to a fresh with-scope symbol, so
checkNotProtected -- looking up whatever identifier was actually
written -- found that symbol instead of r's, and it was never marked
protected.  `with r do f := 5` silently wrote through a `protected var`
parameter with no diagnostic at all.
*)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: grep -c 'protected parameter' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:2
*)

program p(output);
type rec = record f: integer end;
var g: rec;
procedure bumpI(var x: integer); begin x := x + 1 end;
procedure q(protected var r: rec);
begin with r do f := 5 end;
procedure q2(protected var r: rec);
begin with r do bumpI(f) end;
begin g.f := 1; q(g); q2(g) end.
