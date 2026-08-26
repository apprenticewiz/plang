(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: duplicate declaration
*)

module M;
  var n: integer;
  var n: real;
end.
program p;
  import M;
begin writeln(1) end.
