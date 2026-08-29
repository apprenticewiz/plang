(*
The other half of tpFileReady's contract (runtime/plang_file.cpp), distinct
from reset-on-a-nonexistent-file-sets-ioresult-instead-of-crashing.pas: a
file variable that was never Assign/Reset/Rewrite'd at all (F->Fp still
null from its own zero-initialized storage, not from a failed open) is
"file not open" -- InOutRes 103, Borland/FPC's own documented code for
exactly this condition -- not one of plang_tp_posix_to_run_error's errno-
derived codes, since there was no failing OS call to map one from.

Under -std=iso7185 (see
iso7185-writing-to-a-never-opened-file-still-aborts-the-process.pas) the
identical program aborts the process outright; under -std=turbo it must
instead do nothing at all -- no crash, no output written to the file, and
InOutRes left readable afterward -- which is exactly this item's own
non-negotiable P7 split between the two dialects sharing plang_write_file_str.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ioresult=103
CHECK-NEXT:did not crash
*)

var f: text;
begin
  writeln(f, 'this must never be written anywhere');
  writeln('ioresult=', IOResult);
  writeln('did not crash');
end.
