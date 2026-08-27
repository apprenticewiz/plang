(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #261: eof's argument, when given, names the file to test.  An
   ordinary variable was accepted with no diagnostic at all -- CodeGen's
   lowering falls back to testing the standard input file for anything that
   is not a genuine file variable, so this compiled to testing INPUT's own
   eof status and silently discarded 'i'. *)
program p; var i: integer; b: boolean; begin i := 5; b := eof(i) end.

(*
CHECK: 'eof' requires a file argument, got 'integer'
*)
