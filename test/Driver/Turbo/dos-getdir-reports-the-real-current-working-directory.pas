(*
Turbo Tier 4, Cluster C item 6: Dos.GetDir(Drive, Dir) is a getcwd(3)
wrapper -- Drive is a real DOS drive-letter index on real Borland/FPC,
meaningless on POSIX; this implementation's own field-practice-confirmed
reinterpretation (Dos.pas's own header comment, matching real FPC's own
System-unit GetDir) is that EVERY Drive value reports the real current
working directory.  Checked with two different Drive values (0 and 5)
against the SAME real getcwd(3) result, from a real directory this RUN
line itself creates and cds into (not the build directory, so the
expected suffix is pinned and stable).

RUN: rm -rf %t.dir && mkdir -p %t.dir/probedir
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: cd %t.dir/probedir && %run %t | FileCheck %s
*)

program DosGetDir;
uses Dos;
var
  DirA, DirB: string;
begin
  GetDir(0, DirA);
  GetDir(5, DirB);
  Writeln('same-regardless-of-drive: ', DirA = DirB);
  Writeln('ends-with-probedir: ',
          Copy(DirA, Length(DirA) - 7, 8) = 'probedir');
end.

(*
CHECK:same-regardless-of-drive: TRUE
CHECK-NEXT:ends-with-probedir: TRUE
*)
