(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: SeekEof/SeekEoln's base File-kind check is
   generic now (Builtins.def's AK_File row); the text-only follow-on check
   (err_line_proc_not_text, the reverse restriction FilePos/FileSize's
   binary-only one is) stays hand-written -- exercised here too, unchanged
   by the migration. *)
program p;
var i: integer; bf: file of integer; b: boolean;
begin
  b := seekeof(i);
  b := seekeoln(i);
  b := seekeof(bf);
  b := seekeoln(bf)
end.

(*
CHECK: 'seekeof' requires a file argument, got 'integer'
CHECK: 'seekeoln' requires a file argument, got 'integer'
CHECK: 'seekeof' applies to a text file only, not to 'file of integer'
CHECK: 'seekeoln' applies to a text file only, not to 'file of integer'
*)
