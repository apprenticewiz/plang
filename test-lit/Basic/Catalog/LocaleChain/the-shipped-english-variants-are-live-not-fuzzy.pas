(*
en_GB and en_CA are spelling deltas a reviewer is not needed for: both
translate err_label_never_placed with no #, fuzzy marker, so it comes
through without -fdiagnostics-show-fuzzy.

RUN: split-file %s %t.dir
RUN: not %plang -fdiagnostics-language=en_GB %t.dir/label.pas 2> %t.gb.err
RUN: FileCheck %s < %t.gb.err
RUN: not %plang -fdiagnostics-language=en_CA %t.dir/label.pas 2> %t.ca.err
RUN: FileCheck %s < %t.ca.err
*)

(*
CHECK: never defined as a labelled statement
*)

//--- label.pas
program p; label 1; var x : integer; begin x := 1 end.
