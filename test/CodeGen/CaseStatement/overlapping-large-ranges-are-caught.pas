(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: more than one arm
*)

program p(output);
var i: integer;
begin
  case i of
    1..100000000: writeln(1);
    50000000..200000000: writeln(2)
  end
end.
