(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* Issue #739: a same-day sibling PR (#559) had claimed Flush accepts both
   Text and binary files ("confirmed against fpc -Mtp: both accept it").
   Re-checked against a local fpc -Mtp 3.2.2 install: that does NOT hold --
   fpc accepts only Text, refusing both typed and untyped files.  Flush on
   a Text file (not exercised here) is covered by the existing CodeGen
   Turbo flush-*.pas tests, which keep passing. *)
program p;
var
  t: file of Integer;
  u: file;
begin
  flush(t);
  flush(u);
end.

(*
CHECK: 'flush' applies to a text file only, not to 'file of integer'
CHECK: 'flush' applies to a text file only, not to 'file'
*)
