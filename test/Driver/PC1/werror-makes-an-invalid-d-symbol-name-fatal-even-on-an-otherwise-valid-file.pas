(*
Issue #179: compileRequest()'s own refactor (extracting frontendPC1Main's
post-argv-parsing body into a function shared with the new plang::compile()
library API) used a *fresh* DiagnosticsEngine at first, disconnected from
the one the argv parser itself had already been reporting into --
warn_invalid_define_symbol (from an invalid -d/-u symbol name, checked only
once -std=turbo's own Defines loop runs, well after the one
"if (Diags.hasErrors()) return 1;" check right after the argv-parsing loop)
is elevated to an error by -Werror and printed immediately, exactly as
before, but a fresh engine downstream of it forgot that had happened: an
otherwise-valid source file, given a bogus -d name and -Werror together,
would still exit 0 -- the compile "succeeded" (compileRequest's own Diags
started counting from zero again) even though a real, fatal error had
already been reported to stderr moments before.  Fixed by threading the
argv parser's own DiagnosticsEngine into compileRequest by reference
(exactly as it already threads its own SourceManager), the same single
engine Scanner/Parser/Sema/Codegen accumulate into that it always was
before this refactor.
*)

(*
RUN: not %plang_ir -pc1 -std=turbo -Werror -d1notanidentifier %s -o %t.ir > %t.out 2>&1
RUN: FileCheck %s < %t.out
*)

(*
CHECK: error: '1notanidentifier' is not a valid symbol name for -d/-u
*)

program p;
begin end.
