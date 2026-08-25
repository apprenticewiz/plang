(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not a constant
*)

program p(output);
var i, n: integer;
begin
  n := 3; i := 2;
  case i of
    1..n: writeln('in range');
    otherwise writeln('out')
  end
end.
