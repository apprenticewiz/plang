(*
Single (32-bit IEEE-754 binary32) is promoted to double in CodeGen before it
ever reaches the runtime's real formatter -- there is only one formatter, and
the runtime never sees anything but a double.  That promotion is exact, but
only for the BITS the double already holds: it does not manufacture more
precision than a binary32 ever had.  Formatting the promoted value with the
same DecPlaces=16 (seventeen significant digits) a genuine double's default
write gets does not print MORE of the value -- it prints the promotion's own
zero-padding-turned-nonzero tail, bits the original binary32 never had an
opinion about, as if they were significant: `3.14159265358979` stored in a
Single used to write as `3.1415927410125732E+000`, not the ~9-significant-
digit value a binary32 actually holds.  Checked directly against
`fpc -Mtp`, which caps a Single's write at nine significant digits and, for a
field wider than that needs, pads with leading spaces rather than growing
into digits the type does not have.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:         3.14159274E+000
CHECK-NEXT: 3.1415927E+000
CHECK-NEXT:         3.14159274E+000
*)

var
  s: Single;
begin
  s := 3.14159265358979;
  writeln(s);        // default width: capped precision, not double's own
  write(s:15);        // narrower than the cap needs: unaffected by it
  writeln;
  write(s:24);        // wider than the cap needs: padded, not grown
  writeln;
end.
