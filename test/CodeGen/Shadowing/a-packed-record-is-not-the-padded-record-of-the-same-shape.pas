(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: one is packed and the other is not
*)

program p(output);
var a: record x: char; y: integer end;
    b: packed record x: char; y: integer end;
begin a.x := 'Q'; a.y := 7; b := a; writeln(b.x, ' ', b.y:1) end.
