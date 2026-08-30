(*
Tier 3 capstone (integration): the full Assign(Output, ...) redirect
lifecycle in ONE program -- bare Writeln to the real console BEFORE any
redirect, a real redirect to a named file, more Writelns landing in that
file, un-redirecting back to the console (`Assign(Output, ''); Rewrite
(Output);`), and a FINAL bare Writeln landing on the console again --
composing PR #484's own isolated redirect-only and unredirect-only proofs
(assign-output-with-an-empty-name-rebinds-back-to-the-console.pas,
test/CodeGen/Turbo/, which starts already redirected) into the complete
before/during/after shape a real program actually uses this idiom for
(temporarily capturing output to a log file, then going back to talking
to the user).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --check-prefix=CONSOLE %s
RUN: cat assign-output-redirect-and-unredirect-full-lifecycle.log | FileCheck --check-prefix=FILE %s
*)

(*
CONSOLE:before any redirect
CONSOLE-NEXT:back on the console again
CONSOLE-NOT:while redirected

FILE:while redirected, line 1
FILE-NEXT:while redirected, line 2
FILE-NOT:before any redirect
FILE-NOT:back on the console
*)

begin
  writeln('before any redirect');

  Assign(Output, 'assign-output-redirect-and-unredirect-full-lifecycle.log');
  Rewrite(Output);
  writeln('while redirected, line 1');
  writeln('while redirected, line 2');
  Close(Output);

  Assign(Output, '');
  Rewrite(Output);
  writeln('back on the console again');
end.
