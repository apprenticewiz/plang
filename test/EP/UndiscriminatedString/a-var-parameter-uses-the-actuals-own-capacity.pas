(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: string of length 8 assigned to a string(4)
*)

program p(output);
procedure fill(var s: string);
begin s := 'abcdefgh' end;
var a: string(10); b: string(4);
begin
  fill(a); writeln('a=[', a, ']');
  fill(b); writeln('b=[', b, ']')
end.
