(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot assign 'char' to variable of type 'integer'
*)

program p;
type Vec(n: integer) = record data: array[1..n] of integer end;
var v: Vec('a');
begin
  writeln(v.data[1])
end.
