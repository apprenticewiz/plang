(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: allocation size
*)

program p(output);
type buf(tag: integer; cap: integer) = string(cap);
var b: ^buf;
begin
  new(b, 1, -1000000);
  writeln('should not get here')
end.
