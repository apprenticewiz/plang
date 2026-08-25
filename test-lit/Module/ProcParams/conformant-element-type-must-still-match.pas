(*
RUN: not %plang_ir -std=iso10206 -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure show(a: array[lo..hi: integer] of real);
begin writeln(a[lo]:0:1) end;
procedure run(procedure s(a: array[u..v: integer] of integer));
var arr: array[1..2] of integer;
begin arr[1] := 1; s(arr) end;
begin run(show) end.

(*
CHECK: not congruous
*)
