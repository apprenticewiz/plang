(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi there] n=3
CHECK-NEXT:inside [hi there] n=3 cap=10
*)

program p(output);
type buf(cap: integer) = record s: string(cap); n: integer end;
var p: ^buf;
begin
  new(p, 10);
  with p^ do begin s := 'hi there'; n := 3 end;
  writeln('[', p^.s, '] n=', p^.n:1);
  with p^ do writeln('inside [', s, '] n=', n:1, ' cap=', cap:1);
  dispose(p)
end.
