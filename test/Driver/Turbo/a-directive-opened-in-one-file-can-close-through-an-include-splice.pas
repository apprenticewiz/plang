(*
Issue #656: `{$I file}` splices the named file's text in "as if" it had
been typed at the directive itself (Directives.cpp's openInclude/
popInclude, and the include-directive's own doc comment in Scanner.h) --
taken literally, a `{$...}` directive opened before the splice point and
closed after it is one continuous directive, not two unrelated fragments.
part.inc's own text is `{$DEFINE SPLIT` with no closing `}` at all; the
closing `}` lives in the includer, right after the `{$I part.inc}` that
spliced part.inc in.  Before this fix, skipDirective's raw character scan
never crossed a buffer boundary: running out of part.inc's own buffer
mid-directive reported a spurious "unterminated comment" instead of
continuing to look for the real closer back in the includer, exactly the
way ordinary token scanning already does via popInclude() between tokens.

The directive that only closes because of the splice still has to take
real effect once it does: {$IFDEF SPLIT} below must see SPLIT as defined.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/main.pas -o %t.dir/prog
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:defined
*)

//--- part.inc
{$DEFINE SPLIT

//--- main.pas
program spanning_directive;
{$I part.inc} }
{$IFDEF SPLIT}
begin
  writeln('defined')
end.
{$ELSE}
begin
  writeln('not defined')
end.
{$ENDIF}
