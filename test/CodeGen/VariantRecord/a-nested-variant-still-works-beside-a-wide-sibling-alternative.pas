(*
Companion to a-nested-variant-is-laid-out-after-the-fields-around-it.pas:
that file proves a nested variant's own offset arithmetic is correct, but
its sibling alternative is a plain 8-byte real. This is the shape that
was actually found broken -- the nested variant's sibling here is `set of
char`, a 16-aligned type, so the nested alternative's own blob has to be
placed correctly relative to a sibling with a WIDER alignment requirement
than anything in the nested branch itself.

RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 1.5
CHECK-NEXT:yes
*)

program p(output);
type r = record
  case a: boolean of
    true:  (i: integer;
            case b: boolean of
              true:  (x: real);
              false: (y: char));
    false: (s: set of char)
end;
var v: r;
begin
  v.a := true; v.i := 7; v.b := true; v.x := 1.5;
  writeln(v.i, ' ', v.x:0:1);
  v.a := false; v.s := ['m'..'z'];
  if 'p' in v.s then writeln('yes')
end.
