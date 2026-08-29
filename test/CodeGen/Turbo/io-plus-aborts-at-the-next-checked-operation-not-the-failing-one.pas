(*
This item's own load-bearing proof: Turbo's `{$I+}`/`{$I-}` is TEXTUAL and
POSITIONAL, exactly like every other switch this project already models
(CompilerSwitches.def's own top comment) -- so the automatic check the
compiler inserts is decided by the CHECKED STATEMENT's own source position,
never by which call actually produced a failure.

  {$I-} Reset(f); {$I+} Read(f, x);

Reset fails (the file does not exist -- InOutRes 2, "file not found"), but
it runs under `{$I-}`: no check is emitted right after it, so the process
does NOT abort there.  Read runs under `{$I+}`: it IS a checked statement,
so RangeCheckGuards::ioChecksAt(s.Loc) is true at Read's own location, and
CGProcCall.cpp emits `call void @plang_iocheck()` right after it -- making
Read's own checkpoint the one that catches the still-pending error and
aborts, not Reset's.

Which CODE survives to be reported there is the genuinely subtle half of
this proof, and was verified against the local `fpc -Mtp` install rather
than assumed: naively, Read's own tpFileReady call finds f still closed and
would set InOutRes to 103 ("file not open"), which would make 103 the
number this test expects.  That is NOT what real Turbo Pascal/FPC does.
Confirmed empirically (`fpc -Mtp`, this same two-statement shape, and
several further probes -- a second, differently-failing Reset; a Write
instead of a Read; all against the identical pending-error setup): once an
operation under `{$I-}` leaves InOutRes PENDING and UNREAD, a later failing
operation does not overwrite that code with its own -- the FIRST error is
what a later checked position reports, and it is only replaced once an
explicit IOResult call reads (and so clears) it.  So the number this test
pins is 2, Reset's own original code, not 103 -- see runtime/plang_file.cpp's
setInOutResIfClear for the runtime-side half of this fix and its own
empirical write-up.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 2 %run %t %t.does-not-exist.txt 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 2 at $
*)

var f: text; x: integer;
begin
  assign(f, ParamStr(1));
  {$I-}
  reset(f);
  {$I+}
  read(f, x); { the file is still closed: this is the checked statement that aborts }
  writeln('unreachable: the checkpoint above did not fire');
end.
