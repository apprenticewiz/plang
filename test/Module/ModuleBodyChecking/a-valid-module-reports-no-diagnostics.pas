(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

//--- test.pas
module M;
  const k = 3;
  type pair = record a, b: integer end;
  function sum(p: pair): integer; begin sum := p.a + p.b * k end;
end.
program p;
  import M;
var q: pair;
begin q.a := 1; q.b := 2; writeln(sum(q)) end.
