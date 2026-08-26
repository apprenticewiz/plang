(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: no representable result
*)

program p;
var n: integer;
begin
  n := -9223372036854775807; n := n - 1;
  writeln('before'); writeln(n div (-1))
end.
