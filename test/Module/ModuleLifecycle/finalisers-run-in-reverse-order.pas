(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A init
CHECK-NEXT:B init
CHECK-NEXT:body
CHECK-NEXT:B fini
CHECK-NEXT:A fini
*)

//--- test.pas
module A;
 to begin do writeln('A init');
 to end do writeln('A fini');
end.
module B; import A;
 to begin do writeln('B init');
 to end do writeln('B fini');
end.
program p(output); import B; begin writeln('body') end.
