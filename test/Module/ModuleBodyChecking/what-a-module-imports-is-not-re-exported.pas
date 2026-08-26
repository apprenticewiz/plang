(* B imports A but does not declare 'one', so a program importing only B
   must not see it: harvest reads B's own scope, not the enclosing one. *)

(*
RUN: not %plang -std=iso10206 %s -o %t
*)

module A;
  function one: integer; begin one := 1 end;
end.
module B;
  import A;
  function two: integer; begin two := one + 1 end;
end.
program p;
  import B;
begin writeln(one) end.
