(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:init
CHECK-NEXT:body v=7
CHECK-NEXT:fini
*)

//--- mod.pas
module LifeAlone;
  var v: integer;
  to begin do begin v := 7; writeln('init') end;
  to end do writeln('fini');
end.

//--- prog.pas
program p(output); import LifeAlone;
begin writeln('body v=', v) end.
