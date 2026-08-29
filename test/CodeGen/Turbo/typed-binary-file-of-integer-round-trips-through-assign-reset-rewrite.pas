(*
Non-regression/coverage: `file of Integer` (a typed, non-text file) carries
no dialect gate of its own (Sema::resolveType's FileTypeNode arm), so
Write(f, v)/Read(f, v) against one are reachable from Turbo exactly as from
ISO/EP, through plang_write_binary/plang_read_binary -- two of the ~23
functions this item gave a genuinely separate `_turbo` sibling to
(plang_write_binary_turbo/plang_read_binary_turbo, runtime/plang_file.cpp).
Confirms the ordinary, no-error round trip still works correctly through
those new siblings: three integers written, closed, reopened by name (the
Turbo Assign/Reset/Rewrite/Close model PR #478 established, not ISO's own
unnamed-internal-file rewrite(f)), and read back in order.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

var f: file of integer;
    v: integer;
begin
  assign(f, 'typed-binary-file-of-integer-round-trips.dat');
  rewrite(f);
  v := 11; write(f, v);
  v := 22; write(f, v);
  v := 33; write(f, v);
  close(f);

  assign(f, 'typed-binary-file-of-integer-round-trips.dat');
  reset(f);
  read(f, v); writeln(v);
  read(f, v); writeln(v);
  read(f, v); writeln(v);
  close(f);
end.
