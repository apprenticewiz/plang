(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: trunc and round (ISO §6.6.6.3) were named in the same
   unchecked-fallthrough report as sqrt/sin/cos/... above; CodeGen's
   lowering (an unconditional signed-int-to-double conversion) has no case
   for a non-scalar type such as a string or record, and turns a char or
   boolean's raw ordinal value into a meaningless number instead of being
   refused. *)
program p;
var c: char; b: boolean; n: integer;
begin
  c := 'a'; b := true;
  n := trunc(c);
  n := round(b)
end.

(*
CHECK: 'trunc' requires a numeric argument, got 'char'
CHECK: 'round' requires a numeric argument, got 'boolean'
*)
