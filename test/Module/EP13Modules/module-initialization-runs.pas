(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK-DAG: init
CHECK-DAG: body
*)

//--- test.pas
module M;
  function f(x: integer): integer;
  begin f := x end;
  to begin do writeln('init');
end.
program p;
  import M;
begin
  writeln('body')
end.
