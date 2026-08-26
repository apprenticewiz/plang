(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot assign
*)

module M;
  function f(x: integer): integer;
  var s: string(5);
  begin s := 42; f := x end;
end.
program p;
  import M;
begin writeln(f(1)) end.
