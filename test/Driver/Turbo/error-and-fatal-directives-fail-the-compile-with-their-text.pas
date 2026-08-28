(*
{$ERROR text} and {$FATAL text} are the message-directive family's
error-severity pair (lib/Lex/Directives.cpp): each fails the compilation
outright, with the directive's own free-text argument in the diagnostic --
no output file is produced either way. Checked against real `fpc -Mtp`
field practice: {$ERROR} reports and keeps compiling to the end of the
file (collecting whatever else is wrong with it) while {$FATAL} reports
and aborts right there -- a distinction plang's own diagnostic model (see
Scanner.h's dispatchMessageDirective) does not reproduce past giving each
its own message, since both fail the compile either way and truncating the
scanner's input to fake an immediate abort produced nothing but a cascade
of unrelated parser noise. Both are still checked here, kept apart by
their own DiagID, so a future change to that call cannot quietly make one
of them stop failing the compile without a test noticing.
*)

(*
RUN: split-file %s %t.dir

RUN: not %plang -std=turbo %t.dir/err.pas -o %t.dir/err.bin > %t.dir/err.out 2>&1
RUN: FileCheck --check-prefix=ERR %s < %t.dir/err.out
RUN: test ! -e %t.dir/err.bin

RUN: not %plang -std=turbo %t.dir/fatal.pas -o %t.dir/fatal.bin > %t.dir/fatal.out 2>&1
RUN: FileCheck --check-prefix=FATAL %s < %t.dir/fatal.out
RUN: test ! -e %t.dir/fatal.bin
*)

(*
ERR: error: this is bad
FATAL: error: this is worse
*)

//--- err.pas
program err_dir;
{$ERROR this is bad}
begin
  writeln('unreachable')
end.

//--- fatal.pas
program fatal_dir;
{$FATAL this is worse}
begin
  writeln('unreachable')
end.
