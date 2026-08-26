(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'g'
*)

module m;
  function f(x: integer): integer; begin f := x + 1 end;
  function g(x: integer): integer; begin g := x + 2 end;
end.
program p;
  import m only (f => plus1);
begin writeln(plus1(10), ' ', g(10)) end.
