(*
Turbo Tier 4 capstone (integration): the tier's own stated end goal made
concrete -- a single, real program that `uses Crt, Dos, Strings` together
and does something a genuine small Turbo Pascal utility might do: colors a
banner line via Crt.TextColor, lists real fixture files in a real directory
via Dos.FindFirst/FindNext (reporting a real Size read off disk, not a
mock), and upper-cases a name via Strings' real PChar routines
(StrPCopy/StrUpper).  Every prior Cluster C test exercises exactly one
shipped unit; this is the first to link all three real objects into one
program and prove they cooperate.

This runs against the shipped RTL SOURCE resolved through -I%plang_unit_dir
(build-tree units, since a lit run always drives the in-tree build-dir
`plang` binary and has no installed <prefix>/lib/plang/units tier of its
own to find things through -- see %plang_unit_dir's own lit.cfg.py comment).
The complementary claim -- that this ALSO works from a real installed
prefix with NO flags at all, `plang -std=turbo` alone finding and
auto-linking Crt.o/Dos.o/Strings.o on its own -- is exactly what
crt-uses-clause-auto-links-with-no-flags.pas already proves for Crt alone,
and what this capstone's own docs/turbo.md report extends CI's "Check the
install rules" step (.github/workflows/ci.yml) to prove for all three
together, since lit itself has no install-testing capability (see that
item's own comment for why, and this test suite's own README).

RUN: rm -rf %t.dir && mkdir -p %t.dir
RUN: printf 'aa' > %t.dir/report.txt
RUN: printf 'bbb' > %t.dir/notes.txt
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir | tr '\033' 'E' | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:E[0;1;32;40mFile report:
CHECK-NEXT:count=2
CHECK-NEXT:total-size=5
CHECK-NEXT:upper=WIDGET
*)

program RtlCombinedUtility;
uses Crt, Dos, Strings;
var
  F: SearchRec;
  Dir: string;
  Count: Integer;
  TotalSize: Int64;
  NameBuf: array[0..40] of Char;
  PName: PChar;
  NameStr: string;
begin
  TextColor(LightGreen);
  Writeln('File report:');

  Dir := ParamStr(1);
  Count := 0;
  TotalSize := 0;
  FindFirst(Dir + '/*.txt', faAnyFile, F);
  while DosError = 0 do
  begin
    Count := Count + 1;
    TotalSize := TotalSize + F.Size;
    FindNext(F);
  end;
  FindClose(F);
  Writeln('count=', Count);
  Writeln('total-size=', TotalSize);

  NameStr := 'widget';
  PName := StrPCopy(NameBuf, NameStr);
  PName := StrUpper(PName);
  Writeln('upper=', PName);
end.
