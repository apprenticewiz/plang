(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: control variable 'i'
*)

program p(output);
var i: integer;
procedure outer;
procedure nested;
begin i := 99 end;
begin nested end;
begin for i := 1 to 3 do writeln(i) end.
