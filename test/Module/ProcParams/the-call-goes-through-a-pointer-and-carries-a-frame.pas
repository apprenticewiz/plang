(*
RUN: %plang_ir -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
begin ap := f(v) end;
procedure outer;
var base: integer;
  function addbase(x: integer): integer;
  begin addbase := x + base end;
begin base := 1; writeln(ap(addbase, 2)) end;
begin outer end.

(*
CHECK-DAG: define i64 @pas_ap(ptr
CHECK-DAG: asparam
*)
