(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:102
CHECK-NEXT:102
*)

//--- test.pas
module A; function f: integer; begin f := 1 end;
  function g: integer; begin g := 100 end; end.
module B; import A;
  function f: integer; begin f := 2 + g end;
  procedure show; begin writeln(f) end; end.
program p(output); import B qualified;
begin B.show; writeln(B.f) end.
