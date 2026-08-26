(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 10 100
*)

//--- mod.pas
module MangleLeft;
  var v: integer;
  function f: integer; begin f := 1 end;
  procedure bump; begin v := v + 10 end;
end.
module MangleRight;
  var v: integer;
  function f: integer; begin f := 2 end;
  procedure bump; begin v := v + 100 end;
end.

//--- prog.pas
program p(output);
import MangleLeft qualified; import MangleRight qualified;
begin
  MangleLeft.bump; MangleRight.bump;
  writeln(MangleLeft.f, ' ', MangleRight.f, ' ',
          MangleLeft.v, ' ', MangleRight.v)
end.
