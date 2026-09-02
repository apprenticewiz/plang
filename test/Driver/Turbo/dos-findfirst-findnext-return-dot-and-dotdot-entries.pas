(*
Issue #582: plang_dos_findnext (runtime/plang_dos.cpp) used to unconditionally
skip '.'/'..' directory entries before the caller's own attrMatches mask was
even consulted -- real TP7/`fpc -Mtp` field practice (rtl/unix/dos.pp) does
not special-case them at all: they are ordinary directory entries, filtered
only by the caller's own attribute mask, same as any other dotfile (they
carry FaDirectory, and FaHidden via statInto's leading-'.' rule).  A search
with AnyFile (which includes Hidden) now sees both; a search that excludes
Hidden (e.g. Directory alone, without also OR-ing in Hidden) does not.

RUN: rm -rf %t.dir && mkdir -p %t.dir
RUN: printf 'x' > %t.dir/plain.txt
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir | FileCheck %s
*)

program DosFindFirstDotEntries;
uses Dos;
var
  F: SearchRec;
  Dir: string;
  SawDot, SawDotDot, SawPlain: Boolean;
begin
  Dir := ParamStr(1);
  SawDot := False;
  SawDotDot := False;
  SawPlain := False;
  FindFirst(Dir + '/*', AnyFile, F);
  while DosError = 0 do
  begin
    if F.Name = '.' then SawDot := True;
    if F.Name = '..' then SawDotDot := True;
    if F.Name = 'plain.txt' then SawPlain := True;
    FindNext(F);
  end;
  FindClose(F);
  Writeln('saw-dot: ', SawDot);
  Writeln('saw-dotdot: ', SawDotDot);
  Writeln('saw-plain: ', SawPlain);
end.

(*
CHECK:saw-dot: TRUE
CHECK-NEXT:saw-dotdot: TRUE
CHECK-NEXT:saw-plain: TRUE
*)
