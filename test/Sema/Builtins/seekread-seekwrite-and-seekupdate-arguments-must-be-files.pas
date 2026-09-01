(*
RUN: %plang -std=iso10206 -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #557: SeekRead/SeekWrite/SeekUpdate had NO Sema arm of their own
   at all -- Builtins.def tags them AK_Any (arg1 is a value of the file's
   own declared index type, a different kind from arg0's File, so the
   generic single-tag ArgKinds mechanism cannot check both), and they fell
   through checkCallStmt's generic per-argument checkExpr walk with no
   file-kind check whatsoever.  Without this fix, `seekread(i, 1)` for a
   plain integer i compiled with no diagnostic and reinterpreted i's
   storage as a PascalFile* at runtime -- confirmed to segfault. *)
program p;
var i: integer;
begin
  i := 5;
  seekread(i, 1);
  seekwrite(i, 1);
  seekupdate(i, 1)
end.

(*
CHECK: 'seekread' requires a file argument, got 'integer'
CHECK: 'seekwrite' requires a file argument, got 'integer'
CHECK: 'seekupdate' requires a file argument, got 'integer'
*)
