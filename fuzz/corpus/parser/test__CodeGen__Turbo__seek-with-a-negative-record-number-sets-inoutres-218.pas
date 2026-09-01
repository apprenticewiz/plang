(*
Unlike a Seek past the end of file (legal, see
seek-past-the-current-end-of-file-is-not-an-error.pas right next to this
test), a NEGATIVE record number is the actual error case: the underlying
fseek(3) call fails with EINVAL, which plang_tp_posix_to_run_error maps to
InOutRes 218 -- confirmed against `fpc -Mtp` (this project's own local
install) before this test was written and before that mapping existed in
runtime/plang_file.cpp at all (it was ADDED for this item, once this was
the first exercised call site for EINVAL).  The I-minus directive around
the call: this is about InOutRes's own value, not the automatic I-plus
abort.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:218
*)

var
  f: file of Byte;
begin
  assign(f, 'seek-with-a-negative-record-number-sets-inoutres-218.bin');
  rewrite(f);
  write(f, Byte(1));
  write(f, Byte(2));
  write(f, Byte(3));
  {$I-}
  seek(f, -5);
  writeln(IOResult);
  {$I+}
  close(f);
end.
