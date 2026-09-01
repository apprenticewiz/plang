(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #557: same gap as reset-and-rewrite-argument-must-be-a-file.pas,
   exercised on the OTHER of the two size-2 arms -- under -std=turbo,
   reset/rewrite's optional second argument is an integer RecSize rather
   than EP's external filename (see
   reset-and-rewrite-refuse-a-non-integer-second-argument-under-turbo.pas),
   a separate code path that used to have the identical unchecked
   `(void)checkExpr(*S.Args[0])` on arg0. *)
program p;
var i: integer;
begin
  i := 5;
  reset(i);
  rewrite(i);
  reset(i, 128);
  rewrite(i, 128)
end.

(*
CHECK: 'reset' requires a file argument, got 'integer'
CHECK: 'rewrite' requires a file argument, got 'integer'
CHECK: 'reset' requires a file argument, got 'integer'
CHECK: 'rewrite' requires a file argument, got 'integer'
*)
