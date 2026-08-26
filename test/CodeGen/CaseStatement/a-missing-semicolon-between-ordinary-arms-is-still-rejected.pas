(*
RUN: not %plang -std=iso10206 %s -o %t
*)

program p(output);
var i: integer;
begin i := 2;
  case i of
    1: writeln('one')
    2: writeln('two')
  end
end.
