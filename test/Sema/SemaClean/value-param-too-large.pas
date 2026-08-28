(*
RUN: not %plang_ep -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* issue #410: #223's 1 GiB byteSizeOf gate covered local/global 'var'
   declarations but never a by-value parameter, which gets the identical
   fixed-size stack `alloca` in CodeGen (createEntryAlloca at
   CodeGenProcs.cpp:939).  Sema accepted this with no diagnostic and the
   full compile either hung or crashed llc while lowering the alloca. *)

program t;
procedure p(s: string(maxint));
begin
  writeln(length(s))
end;
begin
end.

(*
CHECK: too large to be a value parameter
*)
