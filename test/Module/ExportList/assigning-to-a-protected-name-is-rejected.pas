(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: protected
*)

module m interface;
  export m = (protected count);
  var count: integer;
end.
module m;
  var count: integer;
end.
program p;
  import m;
begin count := 5 end.
