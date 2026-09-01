(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: goto '5'
*)

program p(output);
label 5;
var i: integer;
procedure q;
begin goto 5 end;
begin
  for i := 1 to 2 do begin 5: writeln(i) end;
  q
end.
