(*
Tier 3 gap fix (was: read-of-malformed-numeric-input-does-not-yet-honor-
i-minus-known-gap.pas): a real bug found WHILE WRITING the Tier 3 capstone's
own IOResult-under-{$I-} matrix, confirmed empirically against the local
`fpc -Mtp` 3.2.2 install, and now fixed.

Real Turbo Pascal/FPC treats a malformed numeric token read via
`read`/`readln` (Runtime error 106, "Invalid numeric format") as an
ORDINARY I/O failure subject to `{$I-}`/`{$I+}` exactly like every other
InOutRes code this tier's file model raises: under `{$I-}`, `fpc -Mtp`
sets IOResult to 106, assigns the destination variable 0, and lets the
program keep running -- verified directly:

    program t;
    {$mode tp}
    var i: Integer;
    begin
      i := 999;
      {$I-}
      read(i);
      writeln('ioresult=', IOResult);
      writeln('i=', i);
    end.

    $ echo "12abc" | ./t
    ioresult=106
    i=0

plang's own runtime used to NOT do this: plang_read_file_i64_turbo/
plang_read_file_f64_turbo/plang_read_file_u64_turbo (runtime/plang_file.cpp
-- the entry points BuiltinIO.cpp's emitReadArg actually reaches for a
`-std=turbo` `read`/`readln`, including a bare `read` with no explicit file
variable, since Turbo's own `input` is modeled as a real PascalFile binding)
called plang_tp_runerror(106) directly on a malformed token -- a
`[[noreturn]]` reporter that unconditionally aborts the process INSIDE the
read call, before control ever returned to the caller, so the existing
emitIoCheckIfNeeded machinery (lib/CodeGen/CGProcCall.cpp, wired in after
every read/readln statement and already correctly honoring {$I-}/{$I+} for
every OTHER Turbo I/O failure) never got a chance to run. Fixed by setting
the destination variable to 0 and InOutRes to 106 (via the same
setInOutResIfClear "a pending, unread error is not overwritten" contract
runtime/plang_file.cpp's Reset/Rewrite/Append failures already use) and
returning normally, exactly like every other Turbo I/O failure -- see
plang_read_file_i64_turbo's own comment in runtime/plang_file.cpp for the
full rationale plang_io.cpp's plang_read_i64_turbo/_u64_turbo/_f64_turbo
(the stdin-only, currently-dead-under-`-std=turbo` siblings) also document.

Two cases below: {$I-} continues with IOResult 106 and the destination left
0, and default {$I+} still aborts with "Runtime error 106", matching every
other InOutRes code's own established default-abort behavior (see
default-i-plus-exit-status-matches-the-ioresult-code.pas for the exact
pattern this mirrors).

RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/i-minus.pas -o %t.iminus
RUN: %run %t.iminus < %S/Inputs/malformed-numeric-token.txt | FileCheck --check-prefix=IMINUS %s

RUN: %plang -std=turbo %t.dir/i-plus.pas -o %t.iplus
RUN: %checkexit 106 %run %t.iplus < %S/Inputs/malformed-numeric-token.txt 2> %t.iplus.err
RUN: FileCheck --check-prefix=IPLUS %s < %t.iplus.err
*)

(*
IMINUS: ioresult=106
IMINUS-NEXT: i=0

IPLUS: Runtime error 106 at $
*)

//--- i-minus.pas
var i: Integer;
begin
  i := 999;
  {$I-}
  read(i);
  writeln('ioresult=', IOResult);
  writeln('i=', i);
end.

//--- i-plus.pas
var i: Integer;
begin
  i := 999;
  read(i); { no I-minus anywhere: this is a checked statement, and it fails }
  writeln('unreachable: ioresult=', IOResult, ' i=', i);
end.
