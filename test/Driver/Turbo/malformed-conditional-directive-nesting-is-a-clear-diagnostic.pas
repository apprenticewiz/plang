(*
Four ways a conditional-compilation directive can be structurally wrong,
each its own split-file chunk, each its own DiagID from
lib/Lex/Directives.cpp (err_directive_expects_symbol,
err_directive_no_matching_ifdef, err_directive_else_already_seen,
err_directive_unterminated_conditional): {$IFDEF} with no symbol name;
{$ELSE} with no {$IFDEF}/{$IFNDEF} open at all; a second {$ELSE} for the
same chain; and a live {$IFDEF} whose own {$ENDIF} the file simply never
reaches. None of these four is the "malformed content inside a dead
branch" case (see the sibling ScannerTurbo tests for that one) -- every
directive here is reached through ordinary, live scanning, which is
exactly why each is a real, reported error rather than silently skipped.
*)

(*
RUN: split-file %s %t.dir

RUN: not %plang -std=turbo %t.dir/missing-symbol.pas -o %t.dir/a.bin > %t.dir/a.out 2>&1
RUN: FileCheck --check-prefix=MISSING %s < %t.dir/a.out
RUN: test ! -e %t.dir/a.bin

RUN: not %plang -std=turbo %t.dir/else-without-ifdef.pas -o %t.dir/b.bin > %t.dir/b.out 2>&1
RUN: FileCheck --check-prefix=NOMATCH %s < %t.dir/b.out
RUN: test ! -e %t.dir/b.bin

RUN: not %plang -std=turbo %t.dir/duplicate-else.pas -o %t.dir/c.bin > %t.dir/c.out 2>&1
RUN: FileCheck --check-prefix=DUPELSE %s < %t.dir/c.out
RUN: test ! -e %t.dir/c.bin

RUN: not %plang -std=turbo %t.dir/unterminated.pas -o %t.dir/d.bin > %t.dir/d.out 2>&1
RUN: FileCheck --check-prefix=UNTERM %s < %t.dir/d.out
RUN: test ! -e %t.dir/d.bin
*)

(*
MISSING: error: 'IFDEF' directive expects a single symbol name
NOMATCH: error: 'ELSE' with no matching '{$IFDEF}'/'{$IFNDEF}'
DUPELSE: error: 'ELSE' after '{$ELSE}' for the same '{$IFDEF}'/'{$IFNDEF}'
UNTERM: error: no matching '{$ENDIF}' for this 'IFDEF' directive
*)

//--- missing-symbol.pas
program p;
{$IFDEF}
begin
  writeln('unreachable')
end.

//--- else-without-ifdef.pas
program p;
{$ELSE}
begin
  writeln('unreachable')
end.

//--- duplicate-else.pas
program p;
{$IFDEF X}
{$ELSE}
{$ELSE}
{$ENDIF}
begin
  writeln('unreachable')
end.

//--- unterminated.pas
program p;
{$IFDEF X}
begin
  writeln('unreachable')
end.
