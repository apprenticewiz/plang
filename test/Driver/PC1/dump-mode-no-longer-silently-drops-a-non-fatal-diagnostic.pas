(*
Issue #179's own compileRequest()/frontendPC1Main split has one disclosed,
deliberate behavior difference from the code it replaced (see
compileRequest's call site in frontendPC1Main, Frontend.cpp, for the fuller
account): the OLD -dump-tokens/-dump-parse-tree paths returned straight out
of a withOutput(...) lambda without ever reaching an emitAll() call unless
Diags.hasErrors() was true, so a non-fatal diagnostic recorded along the way
(a HINT-directive or WARNING-directive warning, say) was silently never
printed -- exit code 0, empty stderr, diagnostic gone. The new code funnels
every path through one unconditional post-return emitAll() call, so that
diagnostic is now printed, same as it always was on a normal (non-dump-mode)
compile.

This is an intentional, disclosed side benefit of centralizing diagnostic
collection for plang::compile() (CompilationResult::Diagnostics must be a
complete picture of what happened), not a regression: the exit code is
still 0, and nothing about the diagnostic's own content or the dump mode's
own stdout output changes -- only whether the warning reaches stderr at
all. This test pins down the NEW, correct behavior so nothing drifts back
to silently dropping it.
*)

(*
RUN: %plang_ir -pc1 -std=turbo -dump-tokens %s -o %t.tokens.ir > %t.tokens.out 2>&1
RUN: FileCheck --check-prefix=WARN %s < %t.tokens.out

RUN: %plang_ir -pc1 -std=turbo -dump-parse-tree %s -o %t.tree.ir > %t.tree.out 2>&1
RUN: FileCheck --check-prefix=WARN %s < %t.tree.out
*)

(*
WARN: warning: from hint
*)

program dump_mode_diag;
{$HINT from hint}
begin
end.
