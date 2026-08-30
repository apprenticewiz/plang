(*
Symmetric with the missing-'interface' case: the 'implementation' keyword is
required after the interface section's declarations, even though the
implementation section itself may go on to declare nothing.  Writing 'end.'
straight after the interface declarations (as an EP module's own bodyless
heading would allow) is not legal Turbo unit syntax.
*)

(*
RUN: not %plang_ir -std=turbo -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

unit NoImplementationKeyword;

interface

var X: Integer;

end.

(*
CHECK: expected 'implementation', got 'end'
*)
