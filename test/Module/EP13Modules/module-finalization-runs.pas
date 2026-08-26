(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK-DAG: body
CHECK-DAG: done
*)

//--- test.pas
module M;
  function f(x: integer): integer;
  begin f := x end;
  to end do writeln('done');
end.
program p;
  import M;
begin
  writeln('body')
end.
