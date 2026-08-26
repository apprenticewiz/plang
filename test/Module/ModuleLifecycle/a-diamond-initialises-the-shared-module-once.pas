(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:init base
CHECK-NEXT:init left
CHECK-NEXT:init right
CHECK-NEXT:body
CHECK-NEXT:fini right
CHECK-NEXT:fini left
CHECK-NEXT:fini base
*)

//--- mod.pas
module DiaBase;
  to begin do writeln('init base');
  to end do writeln('fini base');
end.
module DiaLeft; import DiaBase;
  to begin do writeln('init left');
  to end do writeln('fini left');
end.
module DiaRight; import DiaBase;
  to begin do writeln('init right');
  to end do writeln('fini right');
end.

//--- prog.pas
program p(output); import DiaLeft; import DiaRight;
begin writeln('body') end.
