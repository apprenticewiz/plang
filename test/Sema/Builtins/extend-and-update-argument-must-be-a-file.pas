(*
RUN: %plang -std=iso10206 -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #557: unlike reset/rewrite (which at least had a discarded
   `(void)checkExpr` on arg0), extend/update had NO Sema arm of their own
   at all -- they fell through checkCallStmt's generic per-argument
   checkExpr walk with no file-kind check whatsoever, the same "no arm at
   all" gap issue #306's fourth slice closed for get/put/close.  Without
   this fix, `extend(i)`/`update(i)` for a plain integer i compiled with no
   diagnostic and reinterpreted i's storage as a PascalFile* at runtime --
   confirmed to segfault. *)
program p;
var i: integer;
begin
  i := 5;
  extend(i);
  update(i);
  extend(i, 'x.txt');
  update(i, 'x.txt')
end.

(*
CHECK: 'extend' requires a file argument, got 'integer'
CHECK: 'update' requires a file argument, got 'integer'
CHECK: 'extend' requires a file argument, got 'integer'
CHECK: 'update' requires a file argument, got 'integer'
*)
