(*
The default-format baseline (the-default-real-profile-uses-an-uppercase-
exponent.pas) only exercises plang_write_f64/plang_write_f64_e's no-decimals
path.  A caller-given width goes through the same plangFormatReal, threaded
through separately (emitWriteValueFormatted's own dispatch chain in
BuiltinIO.cpp), so it needs its own proof the Upper flag reaches it too.
The fixed-point form (:W:D) has no exponent at all, so it is unaffected by
Upper either way -- included here to show the reversal is exponent-only,
not a wholesale reformat of every real write.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.0000E+000
CHECK-NEXT: 1.000000000000E+000
CHECK-NEXT:    3.1416
*)

program p;
begin
  writeln(1.0:12);
  writeln(1.0:20);
  writeln(3.14159:10:4)
end.
