(*
Four ways an {$I}/{$INCLUDE} can go wrong, each its own DiagID from
lib/Lex/Directives.cpp, each its own split-file scenario: a filename naming
nothing openable (err_directive_include_not_found); no filename at all
(err_directive_include_expects_filename); a file including itself directly
(err_directive_include_cycle); and two files including each other
(err_directive_include_cycle again, from the second one's own openInclude
call).  The two cycle scenarios are the ones that matter most: openInclude
checks OpenIncludePaths -- the on-disk identity of every file currently
open, main file included -- before ever opening a candidate, which is what
turns what would otherwise be unbounded recursion (the included buffer
running out only pops back to an including buffer that immediately hits the
very same {$I} again) into one reported error and a clean, prompt exit.
Confirmed empirically before writing this: `fpc -Mtp` on an equivalent
self-include instead enforces a maximum include depth (16) and reports
that; plang's own cycle check is more precise (it names the file forming
the cycle) and never even reaches a depth limit, since the very first
repeat is caught.
*)

(*
RUN: split-file %s %t.dir

RUN: not %plang -std=turbo %t.dir/missing.pas -o %t.dir/a.bin > %t.dir/a.out 2>&1
RUN: FileCheck --check-prefix=MISSING %s < %t.dir/a.out
RUN: test ! -e %t.dir/a.bin

RUN: not %plang -std=turbo %t.dir/empty.pas -o %t.dir/b.bin > %t.dir/b.out 2>&1
RUN: FileCheck --check-prefix=EMPTY %s < %t.dir/b.out
RUN: test ! -e %t.dir/b.bin

RUN: not %plang -std=turbo %t.dir/self-main.pas -o %t.dir/c.bin > %t.dir/c.out 2>&1
RUN: FileCheck --check-prefix=SELF %s < %t.dir/c.out
RUN: test ! -e %t.dir/c.bin

RUN: not %plang -std=turbo %t.dir/mutual-main.pas -o %t.dir/d.bin > %t.dir/d.out 2>&1
RUN: FileCheck --check-prefix=MUTUAL %s < %t.dir/d.out
RUN: test ! -e %t.dir/d.bin
*)

(*
MISSING: error: cannot open include file 'does-not-exist.inc'
EMPTY: error: 'I' directive expects a filename
SELF: error: include cycle: 'self.inc' is already being included
MUTUAL: error: include cycle: 'mutual-a.inc' is already being included
*)

//--- missing.pas
program missing;
begin
{$I does-not-exist.inc}
end.

//--- empty.pas
program empty;
begin
{$I}
end.

//--- self.inc
{$I self.inc}

//--- self-main.pas
program selfmain;
{$I self.inc}
begin
end.

//--- mutual-a.inc
{$I mutual-b.inc}

//--- mutual-b.inc
{$I mutual-a.inc}

//--- mutual-main.pas
program mutualmain;
{$I mutual-a.inc}
begin
end.
