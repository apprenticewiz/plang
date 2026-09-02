(*
Issue #612: "-c" (compile-only, no link) with more than one .pas input on
the command line -- "plang -c first.pas second.pas" -- kept only the MAIN
file's own object ("first.o", named directly from Opts.outputFile /
defaultOutput). Every extra file's object went through the loop issue
#279's own fix added (see extra-file-objects-do-not-litter-the-cwd-
without-save-temps.pas): a real OS temp file, deleted once the link step
that was supposed to consume it finished. Under "-c", though, compile()
returns right after producing the MAIN file's own object and never reaches
the link step at all -- so "second.o" was compiled, then immediately
deleted by the very cleanup meant to tidy up after a link that never
happened, with nothing left to show for it.

Fixed by keeping every extra file's object under its permanent,
collision-proof flattenedStem name -- the same name -save-temps already
uses, and for the same "this is not just a link-step intermediate"
reason -- whenever Opts.mode will not reach the link step, not only when
-save-temps was explicitly given. "-c" now produces one real, permanent
.o per source, matching gcc's own "gcc -c a.c b.c" behavior (a.o AND b.o,
not just a.o).

Also checks the complementary misuse gcc itself rejects the same way:
a single explicit -o cannot name more than one object "-c" is about to
produce, so it is refused up front, before compiling anything, rather
than silently doing something inconsistent (e.g. keeping only one of the
two, which is exactly the shape of the original bug).
*)

(*
RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -c first.pas second.pas
RUN: test -e %t.dir/first.o
RUN: test -e %t.dir/second.o
RUN: cd %t.dir && %plang first.o -o first_bin
RUN: %run %t.dir/first_bin | FileCheck --check-prefix=FIRST --strict-whitespace --match-full-lines %s
RUN: cd %t.dir && %plang second.o -o second_bin
RUN: %run %t.dir/second_bin | FileCheck --check-prefix=SECOND --strict-whitespace --match-full-lines %s

RUN: not %plang -c -o %t.dir/out.o %t.dir/first.pas %t.dir/second.pas > %t.out 2>&1
RUN: FileCheck --check-prefix=MULTIOUT %s < %t.out
RUN: test ! -e %t.dir/out.o
*)

(*
FIRST:first
SECOND:second
MULTIOUT: error: cannot specify -o with -c when compiling multiple files
*)

//--- first.pas
program first;
begin
  writeln('first')
end.

//--- second.pas
program second;
begin
  writeln('second')
end.
