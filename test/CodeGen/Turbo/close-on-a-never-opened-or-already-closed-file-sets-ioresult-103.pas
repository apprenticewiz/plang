(*
Issue #575: plang_tp_close (runtime/plang_file.cpp) used to skip the
tpFileReady openness guard every sibling Turbo file entry point in this file
already has -- closeStream is itself a silent no-op when F->Fp is already
null, so Close on a file that was only ever Assign'd (never
Reset/Rewrite/Append'ed), or on a file that has already been Close'd once
(a double-close), "succeeded" with InOutRes left at 0 instead of reporting
the 103 ("file not open") `fpc -Mtp` reports for both -- confirmed against a
local `fpc -Mtp` 3.2.2 install.

Issue #738 update: each `writeln('ioresult after ...=', IOResult)` below is
ordinary Turbo I/O -- confirmed against `fpc -Mtp`, whenever InOutRes is
ALREADY pending (103) when one of these starts (the 1st and 3rd, right
after a Close that itself just set 103), its own leading literal is
suppressed too, since IOResult is the first write attempt in that
statement to actually clear InOutRes; only the numeric value prints. The
2nd writeln (right after a Close that SUCCEEDED, InOutRes still 0 the
moment it starts) is unaffected and keeps its full text.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:103
CHECK-NEXT:ioresult after 1st close=0
CHECK-NEXT:103
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
