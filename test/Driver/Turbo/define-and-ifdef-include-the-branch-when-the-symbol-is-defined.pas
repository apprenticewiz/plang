(*
DEFINE and IFDEF are lib/Lex/Directives.cpp's conditional-compilation pair:
DEFINE records a symbol as defined from that point in the source forward
(positional, like a switch -- see LangOptions::Defines's own comment), and
IFDEF includes the statements up to the matching ENDIF only when its symbol
is in that set.  Two independent sources via split-file, one with the DEFINE
and one without, so the "not defined" half is a real absence rather than an
UNDEF undoing it -- proving the branch is skipped by default, not merely
skippable.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/defined.pas -o %t.dir/defined.bin
RUN: %run %t.dir/defined.bin | FileCheck --check-prefix=DEFINED --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/undefined.pas -o %t.dir/undefined.bin
RUN: %run %t.dir/undefined.bin | FileCheck --check-prefix=UNDEFINED --strict-whitespace --match-full-lines %s
*)

(*
DEFINED:debug
DEFINED-NEXT:done
UNDEFINED:done
*)

//--- defined.pas
program p;
{$DEFINE DEBUG}
begin
  {$IFDEF DEBUG}
  writeln('debug');
  {$ENDIF}
  writeln('done')
end.

//--- undefined.pas
program p;
begin
  {$IFDEF DEBUG}
  writeln('debug');
  {$ENDIF}
  writeln('done')
end.
