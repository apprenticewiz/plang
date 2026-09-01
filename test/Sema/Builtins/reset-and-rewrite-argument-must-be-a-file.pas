(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #557: reset/rewrite's FIRST argument -- the file to (re)open --
   had no Sema check of any kind before this fix; both arms did a bare
   `(void)checkExpr(*S.Args[0])`, discarding the type entirely.  Without
   this, CodeGen's FileVarHelpers::fileVarPtr's IdentExpr fast path hands
   back ANY variable's own storage address unchecked, so `reset(i)` for a
   plain integer i compiled with no diagnostic and reinterpreted i's
   storage as a PascalFile* at runtime -- confirmed to segfault.  Exercised
   both with and without the optional second argument, since a bare
   `reset(f)`/`rewrite(f)` (arg1 omitted) used to skip the check entirely
   -- neither of the two size-gated arms matched a single-argument call at
   all. *)
program p;
var i: integer;
begin
  i := 5;
  reset(i);
  rewrite(i);
  reset(i, 'x.txt');
  rewrite(i, 'x.txt')
end.

(*
CHECK: 'reset' requires a file argument, got 'integer'
CHECK: 'rewrite' requires a file argument, got 'integer'
CHECK: 'reset' requires a file argument, got 'integer'
CHECK: 'rewrite' requires a file argument, got 'integer'
*)
