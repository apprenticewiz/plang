(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[outer7end]
CHECK-NEXT:[inner7]
*)

(* One of writestr's own write-parameters can call a function that runs its
   own writestr (here, f's body captures into T while it is being evaluated
   as an argument to the outer writestr capturing into S).  The capture
   state has to nest: T must come out as if the inner writestr were the only
   one running, and S must come out as if the inner call had never touched
   the capture at all -- nothing should reach stdout in between. *)

program p;
var
  S, T: string(30);

function f: integer;
begin
  writestr(T, 'inner', 7);
  f := 7
end;

begin
  writestr(S, 'outer', f(), 'end');
  writeln('[', S, ']');
  writeln('[', T, ']')
end.
