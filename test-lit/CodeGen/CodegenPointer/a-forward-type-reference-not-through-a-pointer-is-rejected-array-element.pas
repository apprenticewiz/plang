(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: used here before its declaration
*)

(*
EP Sec6.2.1(k) permits declaration parts in any order, but is explicit that
the prohibition of forward references in declarations is retained -- the
pointer's domain type is the one sanctioned exception (ISO Sec6.2.2.9). An
array element naming a type declared LATER in the same type-definition-part
is not that exception. (Companion case: -record-field.pas.)
*)

program p;
type t = array[1..5] of u;
type u = integer;
var x: t;
begin x[1] := 5; writeln(x[1]) end.
