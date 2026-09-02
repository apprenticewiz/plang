(*
Real Turbo Pascal field practice: once InOutRes is nonzero (an error is
PENDING, not yet read-and-cleared through IOResult), Eof(f)/Eoln(f) report
TRUE regardless of the file's actual position -- runtime/plang_file.cpp's
plang_eof_file_turbo/plang_eoln_file_turbo check InOutRes BEFORE anything
about the file itself.  This is what lets `while not Eof(f) do ...` under a
later item's I-minus directive actually TERMINATE once a read starts
failing, instead of retrying the same broken read forever.

InOutRes is forced directly by assignment (`InOutRes := 5`) rather than by
provoking a genuine failing read mid-stream: this item's own fileReady
choke point only ever sets InOutRes when a file is NOT OPEN (Reset/Rewrite/
Append's own open failure, or an operation against a never-opened file),
and every one of those leaves the file itself closed -- so there is no
"file stays open but a later operation on it fails" case for THIS item to
provoke yet (that is a later I-plus/I-minus item's own job, converting
trapOnStreamError's still-unconditionally-aborting checks).  Registering
InOutRes as an ordinary assignable predefined Var (Sema::registerBuiltins,
matching real Turbo Pascal/FPC field practice, where InOutRes genuinely is
a plain global and not merely IOResult's own private state) is what makes
testing the Eof/Eoln behavior in ISOLATION possible without waiting on
that later item.

The file stays open and mid-stream throughout (Readln consumed only the
first line) -- eof-before and eof-after-clear both report false from the
SAME real position, proving the true report in between really is about
InOutRes and not a side effect of the forced assignment moving anything.

`{$I-}` from the forced assignment on: this file is about Eof/Eoln's own
InOutRes-pending check, not about the automatic `{$I+}` check a later part
of this same item adds -- under that check's real default (`{$I+}` ON
unless a program says otherwise), the very first `writeln` after `InOutRes
:= 5` would itself abort the process, since InOutRes is nonzero right there
too.  See io-plus-aborts-at-the-next-checked-operation-not-the-failing-
one.pas for the check's own dedicated coverage -- Reset/Readln above stay
unguarded by `{$I-}` since neither can fail against this file (a lit
fixture the RUN line itself just created).

Issue #738 update: `InOutRes := 5` leaves an error PENDING and UNREAD --
which now (correctly, per `fpc -Mtp`) suppresses every OTHER Turbo I/O
call too, not just Eof/Eoln's own read of it. Eof(f)/Eoln(f) themselves
only ever READ InOutRes, never clear it (only IOResult does), so the two
`writeln`s that report 'eof-during-error'/'eoln-during-error' are
THEMSELVES suppressed outright -- confirmed against `fpc -Mtp`: neither
prints anything at all, not even a blank line, since the suppression
covers the trailing newline write too. The very next `writeln` (whose
SECOND argument is the IOResult call that finally clears InOutRes) loses
only its own leading literal ('cleared-ioresult: ') for the identical
reason every other test in this item's own fix does; the two 'after-clear'
writelns that follow run with InOutRes already back at 0 and are
unaffected.

RUN: printf 'line one\nline two\n' > %t.txt
RUN: %plang -std=turbo %s -o %t
RUN: %run %t %t.txt | FileCheck %s
*)

(*
CHECK:eof-before: false
CHECK-NEXT:eoln-before: false
CHECK-NEXT:5
CHECK-NEXT:eof-after-clear: false
CHECK-NEXT:eoln-after-clear: false
*)

var f: text; s: string;
begin
  assign(f, ParamStr(1));
  reset(f);
  readln(f, s); { consumes "line one"; leaves f positioned at "line two" }

  if Eof(f)  then writeln('eof-before: true')  else writeln('eof-before: false');
  if Eoln(f) then writeln('eoln-before: true') else writeln('eoln-before: false');

  {$I-}
  InOutRes := 5;

  if Eof(f)  then writeln('eof-during-error: true')  else writeln('eof-during-error: false');
  if Eoln(f) then writeln('eoln-during-error: true') else writeln('eoln-during-error: false');

  writeln('cleared-ioresult: ', IOResult);

  if Eof(f)  then writeln('eof-after-clear: true')  else writeln('eof-after-clear: false');
  if Eoln(f) then writeln('eoln-after-clear: true') else writeln('eoln-after-clear: false');
end.
