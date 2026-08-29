(*
-std=turbo: `Assign(f, '')` -- an empty name -- followed by Rewrite/Append
is the established TP idiom for rebinding a file back to the console
(confirmed against `fpc -Mtp`; the very first Cluster A item to reach
Output specifically).  This confirms the idiom works for the PREDEFINED
Output variable too, not just an ordinary program-declared file: after a
real redirect to a named file, `Assign(Output, ''); Rewrite(Output);`
sends a following bare Writeln back to the console rather than leaving it
stuck on the file.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: cat assign-output-with-an-empty-name-rebinds-back-to-the-console.txt | FileCheck --check-prefix=FILE %s
*)

(*
CHECK:back on the console
FILE:only in the file
*)

begin
  Assign(Output, 'assign-output-with-an-empty-name-rebinds-back-to-the-console.txt');
  Rewrite(Output);
  Writeln('only in the file');
  Close(Output);

  Assign(Output, '');
  Rewrite(Output);
  Writeln('back on the console');
end.
