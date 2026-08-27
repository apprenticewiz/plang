(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* EP §6.7.3.7.1 NOTE 2: "The object denoted by a bound-identifier is
   neither constant nor a variable" -- it may be read but never assigned, so
   writing through it must be a compile-time error rather than silently
   corrupting the bound the caller passed in. *)

(*
ERR: conformant-array bound
*)

program p;
procedure corrupt(var a: array [l..h: integer] of integer);
begin
  l := 99
end;
var arr: array [1..3] of integer;
begin
  corrupt(arr)
end.
