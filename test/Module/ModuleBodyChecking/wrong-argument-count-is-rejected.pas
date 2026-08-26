(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: expects 2 argument
ERR-ABSENT-NOT: IR verification
*)

module M;
  function g(a, b: integer): integer; begin g := a + b end;
  function f(x: integer): integer; begin f := g(x) end;
end.
program p;
  import M;
begin writeln(f(1)) end.
