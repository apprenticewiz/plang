(*
issue #613: the -dump-parse-tree branch (Frontend.cpp's compileRequest)
hardcoded finish(OSS.str(), true) -- unlike -dump-tokens's own
finish(OSS.str(), !Diags.hasErrors()) just above it -- so a parse that
recorded a promoted error (here, a $WARNING directive turned fatal by
-Werror) still printed the parse tree AND reported success: CLI exit 0,
CompilationResult::Success == true, even though Diags.hasErrors() was
already true.  Now matches -dump-tokens's own convention.
*)

(*
RUN: not %plang_ir -pc1 -std=turbo -dump-parse-tree -Werror %s > %t.out 2>&1
RUN: FileCheck %s < %t.out
*)

(*
CHECK: error: promoted warning
CHECK: (program p
*)

program p(output);
{$WARNING promoted warning}
begin
  writeln(1)
end.
