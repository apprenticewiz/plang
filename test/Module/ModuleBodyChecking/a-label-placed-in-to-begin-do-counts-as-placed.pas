(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:init
CHECK-NEXT:body
*)

//--- test.pas
module M;
  label 1;
  to begin do 1: writeln('init');
end.
program p;
  import M;
begin writeln('body') end.
