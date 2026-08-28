(*
RUN: not %plang_ep -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* issue #410: #223's 1 GiB byteSizeOf gate covered local/global 'var'
   declarations but never a function's return type, which gets the identical
   fixed-size stack `alloca` in CodeGen (curRetAlloca at
   CodeGenProcs.cpp:681).  Sema accepted this with no diagnostic and the
   full compile either hung or crashed llc while lowering the alloca. *)

program t;
function f: string(maxint);
begin
  f := 'x'
end;
begin
end.

(*
CHECK: too large to be a function result
*)
