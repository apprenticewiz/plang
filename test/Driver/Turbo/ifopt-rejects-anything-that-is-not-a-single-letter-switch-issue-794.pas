(*
{$IFOPT} only ever takes a single letter immediately followed by '+' or '-'
(dispatchConditionalDirective's own "ifopt" branch in
lib/Lex/Directives.cpp) -- confirmed against `fpc -Mtp`, which rejects a
long-name spelling as an "Illegal compiler switch" even though the exact
same long name is perfectly valid as a plain `{$RANGECHECKS+}` switch
directive.  Three ways an {$IFOPT} argument can fail that:
  - longname.pas: a valid long-name switch spelling ("RANGECHECKS+"), which
    dispatchSwitchDirective itself would happily accept -- {$IFOPT} does
    not fall back to it.
  - letterless.pas: OBJECTCHECKS, one of the two switches
    (CompilerSwitches.def's own '\0' Letter column) that has no single-letter
    spelling in real Borland/FPC at all, so it can never be a valid
    {$IFOPT} argument no matter how it is written.
  - nosign.pas: a bare letter with no '+'/'-' at all.
Each is a real, reported error (err_directive_ifopt_bad_switch) rather than
silently taking either branch -- and each still balances its own
{$ELSE}/{$ENDIF} rather than cascading into an unrelated
err_directive_no_matching_ifdef, the same way a missing {$IFDEF} symbol
name already does.
*)

(*
RUN: split-file %s %t.dir

RUN: not %plang -std=turbo %t.dir/longname.pas -o %t.dir/a.bin > %t.dir/a.out 2>&1
RUN: FileCheck --check-prefix=LONGNAME %s < %t.dir/a.out
RUN: test ! -e %t.dir/a.bin

RUN: not %plang -std=turbo %t.dir/letterless.pas -o %t.dir/b.bin > %t.dir/b.out 2>&1
RUN: FileCheck --check-prefix=LETTERLESS %s < %t.dir/b.out
RUN: test ! -e %t.dir/b.bin

RUN: not %plang -std=turbo %t.dir/nosign.pas -o %t.dir/c.bin > %t.dir/c.out 2>&1
RUN: FileCheck --check-prefix=NOSIGN %s < %t.dir/c.out
RUN: test ! -e %t.dir/c.bin
*)

(*
LONGNAME: error: illegal compiler switch 'RANGECHECKS+'
LETTERLESS: error: illegal compiler switch 'OBJECTCHECKS+'
NOSIGN: error: illegal compiler switch 'R'
*)

//--- longname.pas
program longname;
begin
  {$IFOPT RANGECHECKS+}
  writeln('unreachable')
  {$ENDIF}
end.

//--- letterless.pas
program letterless;
begin
  {$IFOPT OBJECTCHECKS+}
  writeln('unreachable')
  {$ENDIF}
end.

//--- nosign.pas
program nosign;
begin
  {$IFOPT R}
  writeln('unreachable')
  {$ENDIF}
end.
