(*
FreeMem's first argument must be a pointer, exactly like GetMem's own
(getmem-first-argument-must-be-a-pointer.pas) -- but FreeMem's is a plain
value argument, not `var` (runtime/plang_sys.cpp's plang_tp_freemem's own
comment), so this only exercises the pointer-kind check, not
err_var_param_needs_lvalue.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'freemem' expects a pointer as its first argument, got 'integer'
*)

program p;
var x: Integer;
begin FreeMem(x) end.
