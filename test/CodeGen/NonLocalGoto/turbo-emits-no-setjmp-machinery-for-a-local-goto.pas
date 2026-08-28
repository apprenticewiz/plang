(*
Once Sema rejects every non-local goto under -std=turbo (checkGoto's
Opts.turbo() gate, SemaStmt.cpp), a Turbo program's every remaining goto is
local, so LabelGotoEngine::nonLocalTargets(block) is empty for every block a
Turbo program has, and openLabelScope's own fast path (`if
(nonLocalTargets(block).empty()) return;`, LabelGotoEngine.cpp) takes over
before any of the setjmp/longjmp/switch-dispatch machinery is planted --
this is a claim about CodeGen needing zero changes, checked here by
inspecting the IR it actually emits rather than just reading the source.
Contrast leaves-every-activation-between-it-and-the-label.pas (this
directory), whose ISO 7185 non-local goto -- structurally the same program,
minus -std=turbo -- does emit exactly that machinery.
*)

(*
RUN: %plang_ir -std=turbo -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK-NOT: setjmp
CHECK-NOT: goto.buf
CHECK-NOT: switch
*)

program p(output);
label 1;
begin
  writeln('before');
  goto 1;
  writeln('skipped');
1:
  writeln('after')
end.
