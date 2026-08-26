(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot assign 'integer' to variable of type 'boolean'
*)

program p;
type Box(c: boolean) = record x: integer end;
var b: ^Box;
begin
  new(b, 42);
  if b^.c then writeln('true branch') else writeln('false branch');
  writeln(b^.c)
end.
