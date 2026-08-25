(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:inner
CHECK-NEXT:landed
*)

program p(output);
label 1;
procedure outer;
  procedure inner;
  begin writeln('inner'); goto 1 end;
begin inner; writeln('not reached in outer') end;
begin
  outer;
  writeln('not reached in main');
1:
  writeln('landed')
end.
