(*
Real Turbo Pascal's read-and-clear IOResult contract (this item's own
runtime/plang_sys.cpp plang_tp_ioresult, the ONE place the clear itself
happens): a first IOResult call after a failing operation reports the
pending InOutRes code, and an immediately following one reports 0, even
with nothing else run in between.  Exercised through BOTH syntactic forms
-- IOResult() (explicit call) and IOResult (bare) -- in the same program,
proving CGExprCore.cpp's bare-identifier arm and CGFuncCall.cpp's
explicit-call arm really do reach the identical runtime accessor and not
two independently (and possibly inconsistently) implemented ones.

The pending error itself comes from Reset failing on a file that does not
exist -- runtime/plang_file.cpp's plang_tp_reset, on a failed fopen, now
sets InOutRes via plang_tp_posix_to_run_error(ENOENT) (= 2) and returns
rather than aborting the process the way the ISO reset/rewrite functions
still do; this is also this item's own "a genuine I/O error sets a sensible
InOutRes rather than crashing" requirement, exercised concretely.  Reset is
called TWICE, to prove the SAME failure can set InOutRes again after a
prior IOResult call already cleared it -- InOutRes is not a one-shot latch.

`{$I-}` throughout: this file is about IOResult's own read-and-clear
contract, not about the automatic `{$I+}` check a later part of this same
item adds -- under that check's real default (`{$I+}` ON unless a program
says otherwise), the first failing Reset would itself abort the process
before either `writeln(IOResult...)` line ran.  See io-plus-aborts-at-the-
next-checked-operation-not-the-failing-one.pas for the check's own
dedicated coverage.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:2
CHECK-NEXT:0
CHECK-NEXT:2
CHECK-NEXT:0
*)

var f: text;
begin
  {$I-}
  assign(f, 'ioresult-reads-and-clears-inoutres-this-file-does-not-exist.txt');
  reset(f);
  writeln(IOResult());
  writeln(IOResult());

  reset(f);
  writeln(IOResult);
  writeln(IOResult);
end.
