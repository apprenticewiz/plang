(*
A two-level nested include, where BOTH levels are found only through
-Fi<dir> -- an integration gap neither existing include test covers alone.
nested-includes-resolve-relative-to-their-own-including-file.pas nests three
files deep, but every one of them is found by the "including file's own
directory" rule, never through -Fi at all. fi-search-path-is-tried-after-
the-including-files-own-directory.pas exercises -Fi, but only for a single,
top-level {$I}; the file -Fi finds there does not itself {$I} anything.

level1.inc physically lives in lib1/, found via the first -Fi when main.pas
(in a separate root/ directory) includes it. It then does {$I level2.inc},
and level2.inc is NOT beside it in lib1/ -- it lives only in lib2/, the
SECOND -Fi directory. So resolving level2.inc must, at that second level of
nesting, again fall through "own directory" (lib1/, which does not have it)
to the full -Fi list (tried in order, lib1 then lib2) -- proving
resolveIncludePath's fallback chain is applied uniformly at every include
depth from the CURRENTLY active buffer, not just once for the outermost
file. The negative control (omitting the second -Fi) confirms level2.inc is
not found by some other route -- that the two-level chain genuinely depends
on both search-path entries, not merely on the first.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo -Fi %t.dir/lib1 -Fi %t.dir/lib2 %t.dir/root/main.pas -o %t.dir/prog
RUN: %run %t.dir/prog | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: not %plang -std=turbo -Fi %t.dir/lib1 %t.dir/root/main.pas -o %t.dir/noflag.bin > %t.dir/noflag.out 2>&1
RUN: FileCheck --check-prefix=NOTFOUND %s < %t.dir/noflag.out
RUN: test ! -e %t.dir/noflag.bin
*)

(*
RAN:main before
RAN-NEXT:level1 before
RAN-NEXT:level2
RAN-NEXT:level1 after
RAN-NEXT:main after
NOTFOUND: cannot open include file 'level2.inc'
*)

//--- root/main.pas
program nestfi_main;
begin
  writeln('main before');
  {$I level1.inc}
  writeln('main after')
end.

//--- lib1/level1.inc
  writeln('level1 before');
  {$I level2.inc}
  writeln('level1 after');

//--- lib2/level2.inc
  writeln('level2');
