(*
GetMem's first argument must be a pointer variable -- the same rule new's
own first argument already gets (err_new_arg_not_pointer), reported here
through the analogous err_heap_arg_not_pointer instead so the message names
the builtin that was actually called.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'getmem' expects a pointer as its first argument, got 'integer'
*)

program p;
var x: Integer;
begin GetMem(x, 4) end.
