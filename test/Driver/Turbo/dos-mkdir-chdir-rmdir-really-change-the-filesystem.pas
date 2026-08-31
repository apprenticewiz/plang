(*
Turbo Tier 4, Cluster C item 6: Dos.MkDir/ChDir/RmDir are real mkdir(2)/
chdir(2)/rmdir(2) wrappers (runtime/plang_dos.cpp's own plang_dos_mkdir/
plang_dos_chdir/plang_dos_rmdir), reached through CodeGen's own Dos-
intrinsic recognizer (their own Dir parameter is a `string` VALUE
parameter -- this item's own report).  Checked against REAL filesystem
state before and after each step, not just the program's own report of
DosError: `test -d`/`not test -d` on the RUN lines themselves, following
this tier's own established "real fixture state, not a mock" convention.

RUN: rm -rf %t.dir && mkdir -p %t.dir
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: not test -d %t.dir/newsub
RUN: cd %t.dir && %run %t newsub | FileCheck %s
RUN: not test -d %t.dir/newsub
*)

program DosMkDirChDirRmDir;
uses Dos;
var
  Dir, Cwd: string;
begin
  Dir := ParamStr(1);
  MkDir(Dir);
  Writeln('mkdir-doserror: ', DosError);
  ChDir(Dir);
  Writeln('chdir-doserror: ', DosError);
  GetDir(0, Cwd);
  { The real cwd after ChDir(Dir) must now END with Dir -- the whole path
    is not fixed (the RUN line's own %t.dir varies per test invocation). }
  Writeln('cwd-ends-with-dir: ', Copy(Cwd, Length(Cwd) - Length(Dir) + 1, Length(Dir)) = Dir);
  ChDir('..');
  RmDir(Dir);
  Writeln('rmdir-doserror: ', DosError);
end.

(*
CHECK:mkdir-doserror: 0
CHECK-NEXT:chdir-doserror: 0
CHECK-NEXT:cwd-ends-with-dir: TRUE
CHECK-NEXT:rmdir-doserror: 0
*)
