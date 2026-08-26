(*
"no such file" has nowhere to point, so there is no line to quote.

RUN: %plang_ir -pc1 /nonexistent_file_plang_test.pas > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=NOCARET %s < %t.out
RUN: grep -c 'error: ' %t.out | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
NOCARET-NOT: ^
COUNT:1
*)
