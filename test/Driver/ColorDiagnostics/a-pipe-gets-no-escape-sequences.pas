(*
Two processes print diagnostics, and both used to decide color for
themselves by probing stderr, with no way to be told otherwise. popen gives
a pipe, not a terminal, so no escape sequences is the default answer.

RUN: %plang_ir nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: [
*)
