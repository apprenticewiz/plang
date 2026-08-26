(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'nope' is not exported
*)

module m;
  function f: integer; begin f := 1 end;
end.
program p;
  import m only (f, nope);
begin writeln(f()) end.
