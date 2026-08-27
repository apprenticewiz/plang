(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: same gap as eof, one builtin over -- see
   eof-argument-must-be-a-file.pas.  eoln's own "must be a text file"
   check (err_line_proc_not_text, covered by
   test/CodeGen/TextOnlyProcedures/writeln-page-and-eoln-refuse-a-non-text-file-eoln.pas)
   never fired for a non-file argument in the first place, since it only
   compares the file's component type and an integer has none. *)
program p; var i: integer; b: boolean; begin i := 5; b := eoln(i) end.

(*
CHECK: 'eoln' requires a file argument, got 'integer'
*)
