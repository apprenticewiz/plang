(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: undefined identifier 'nosuchthing'
*)

module M;
  var n: integer;
  to begin do n := nosuchthing;
end.
program p;
  import M;
begin writeln(n) end.
