(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:init base
CHECK-NEXT:init mid
CHECK-NEXT:body w=2
CHECK-NEXT:fini mid
CHECK-NEXT:fini base
*)

//--- mod.pas
module LifeBase;
  var v: integer;
  to begin do begin v := 1; writeln('init base') end;
  to end do writeln('fini base');
end.
module LifeMid;
  import LifeBase;
  var w: integer;
  to begin do begin w := v + 1; writeln('init mid') end;
  to end do writeln('fini mid');
end.

//--- prog.pas
program p(output); import LifeMid;
begin writeln('body w=', w) end.
