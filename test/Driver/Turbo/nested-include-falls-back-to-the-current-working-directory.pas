(*
Issue #657: `fpc -Mtp`'s own include search always tries the compiler's
current working directory as one of its fixed steps, independent of which
file is doing the including -- so a NESTED {$I} (one reached from inside
an already-included file) can still resolve a project-root-relative path
even though its own including file's directory (resolveIncludePath's first
candidate, confirmed correct by nested-includes-resolve-relative-to-their-
own-including-file.pas) is somewhere else entirely.  Before this fix,
resolveIncludePath's search order was [including file's own directory,
then -Fi<dir>] with no cwd step at all, so this nested, project-root-
relative include failed where `fpc -Mtp` succeeds.

Layout: main.pas (in the project root, which is also the cwd this RUN line
cds into before compiling) includes inc/outerB.inc by a path relative to
main.pas's own directory -- ordinary, unaffected by this fix.  outerB.inc
then includes 'inc/innerB.inc' -- a path that is relative to the PROJECT
ROOT, not to outerB.inc's own directory (inc/inc/innerB.inc, which does
not exist).  Only the new cwd fallback finds it.
*)

(*
RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=turbo main.pas -o prog
RUN: cd %t.dir && %run ./prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:inner ok
*)

//--- main.pas
program nested_cwd_fallback;
begin
{$I inc/outerB.inc}
end.

//--- inc/outerB.inc
{$I 'inc/innerB.inc'}

//--- inc/innerB.inc
  writeln('inner ok');
