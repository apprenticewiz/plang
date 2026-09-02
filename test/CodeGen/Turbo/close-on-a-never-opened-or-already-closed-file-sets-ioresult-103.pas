(*
Issue #575: plang_tp_close (runtime/plang_file.cpp) used to skip the
tpFileReady openness guard every sibling Turbo file entry point in this file
already has -- closeStream is itself a silent no-op when F->Fp is already
null, so Close on a file that was only ever Assign'd (never
Reset/Rewrite/Append'ed), or on a file that has already been Close'd once
(a double-close), "succeeded" with InOutRes left at 0 instead of reporting
the 103 ("file not open") `fpc -Mtp` reports for both -- confirmed against a
local `fpc -Mtp` 3.2.2 install.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ioresult after close on never-opened file=103
CHECK-NEXT:ioresult after 1st close=0
CHECK-NEXT:ioresult after 2nd close (double-close)=103
*)

var
  neverOpened, twiceClosed: text;
begin
  {$I-}
  assign(neverOpened, 'close-on-a-never-opened-or-already-closed-file-sets-ioresult-103-1.txt');
  close(neverOpened);
  writeln('ioresult after close on never-opened file=', IOResult);

  assign(twiceClosed, 'close-on-a-never-opened-or-already-closed-file-sets-ioresult-103-2.txt');
  rewrite(twiceClosed);
  writeln(twiceClosed, 'hi');
  close(twiceClosed);
  writeln('ioresult after 1st close=', IOResult);
  close(twiceClosed);
  writeln('ioresult after 2nd close (double-close)=', IOResult);
end.
