(*
EP §6.4.2.5: a restricted type's components are off limits by any spelling
of a component-access.  `x.f := 1` is correctly rejected
(err_restricted_component), but pushWithScope checked the with-expression's
Kind was Record and never asked isRestricted(), so `with x do f := 1`
exposed the very fields the restriction hides -- the two spellings of one
access disagreed.
*)

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: components are not accessible
*)

program p(output);
type r = record f: integer end;
     rr = restricted r;
var x: rr;
begin
  with x do f := 1
end.
