(*
-Fi<dir> (Options.def, threaded to LangOptions::IncludeSearchPaths the same
way -I already threads ModuleSearchPaths -- see that field's own comment for
why it is a deliberately separate flag and list from -I/.pmi) adds a
directory resolveIncludePath falls back to once the including file's own
directory has been tried and failed.  Two split-file scenarios prove both
halves of that order:

  - "order": greeting.inc exists BOTH next to main.pas and inside extra/,
    with different text in each.  If -Fi were searched first, or the two
    were tried in the wrong order, the program would print "from -Fi"; it
    prints "local" instead, proving the including file's own directory
    really is tried first.

  - "fallback": greeting.inc exists ONLY inside extra/, nowhere near
    main.pas.  Without -Fi this is err_directive_include_not_found (checked
    first, as the regression -Fi itself would otherwise silently paper
    over); with -Fi extra it is found and runs.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo -Fi %t.dir/order/extra %t.dir/order/main.pas -o %t.dir/order.bin
RUN: %run %t.dir/order.bin | FileCheck --check-prefix=OWNDIR --strict-whitespace --match-full-lines %s

RUN: not %plang -std=turbo %t.dir/fallback/main.pas -o %t.dir/fallback.noflag.bin > %t.dir/fallback.noflag.out 2>&1
RUN: FileCheck --check-prefix=NOTFOUND %s < %t.dir/fallback.noflag.out
RUN: test ! -e %t.dir/fallback.noflag.bin

RUN: %plang -std=turbo -Fi%t.dir/fallback/extra %t.dir/fallback/main.pas -o %t.dir/fallback.bin
RUN: %run %t.dir/fallback.bin | FileCheck --check-prefix=VIAFI --strict-whitespace --match-full-lines %s
*)

(*
OWNDIR:local
NOTFOUND: cannot open include file 'greeting.inc'
VIAFI:from -Fi
*)

//--- order/main.pas
program ordermain;
begin
{$I greeting.inc}
end.

//--- order/greeting.inc
  writeln('local');

//--- order/extra/greeting.inc
  writeln('from -Fi');

//--- fallback/main.pas
program fallbackmain;
begin
{$I greeting.inc}
end.

//--- fallback/extra/greeting.inc
  writeln('from -Fi');
