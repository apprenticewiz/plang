(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot assign 'integer' to variable of type 'char'
*)

program p;
type Box(c: char) = record x: integer end;
var b: Box(65);
begin
  writeln(b.x)
end.
