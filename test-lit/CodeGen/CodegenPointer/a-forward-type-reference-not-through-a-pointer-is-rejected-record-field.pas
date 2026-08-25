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
pointer's domain type is the one sanctioned exception (ISO Sec6.2.2.9). A
record field naming a type declared LATER in the same type-definition-part
is not that exception. (Companion case: -array-element.pas.)
*)

program p;
type t = record f: u end;
type u = integer;
var x: t;
begin x.f := 5; writeln(x.f) end.
