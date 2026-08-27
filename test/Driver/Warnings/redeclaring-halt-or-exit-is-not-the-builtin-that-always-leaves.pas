(*
RUN: %plang -std=iso10206 %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
ERR-ABSENT-NOT: cannot be reached
CHECK:user exit
CHECK-NEXT:past exit
CHECK-NEXT:user halt
CHECK-NEXT:past halt
*)

program p(output);
procedure exit; begin writeln('user exit') end;
procedure halt; begin writeln('user halt') end;
begin
    exit; writeln('past exit');
    halt; writeln('past halt')
end.
