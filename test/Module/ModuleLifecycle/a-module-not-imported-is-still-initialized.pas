(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:solo init
CHECK-NEXT:body
CHECK-NEXT:solo fini
*)

//--- test.pas
module Solo;
 to begin do writeln('solo init');
 to end do writeln('solo fini');
end.
program p(output); begin writeln('body') end.
