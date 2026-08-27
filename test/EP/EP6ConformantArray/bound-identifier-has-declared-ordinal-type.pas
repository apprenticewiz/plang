(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* EP §6.7.3.7: a conformant-array bound identifier is typed with the
   dimension's declared ordinal type (the OrdType of its index-type-
   specification), not hardcoded to integer -- comparing a bound against a
   value of that declared type must typecheck, here an enumeration. *)

(*
CHECK:true
CHECK-NEXT:false
*)

program p;
type color = (red, green, blue);
procedure showBounds(var a: array [lo..hi: color] of integer);
begin
  writeln(lo = red);
  writeln(hi = red)
end;
var arr: array [red..blue] of integer;
begin
  showBounds(arr)
end.
