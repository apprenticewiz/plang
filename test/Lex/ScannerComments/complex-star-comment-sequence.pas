(*
From scantst.pas line 9: six identifiers separated by increasingly
star-heavy paren comments, ending with the tricky close-inside-body form.

RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

(**) u (***) v (****) w (*****) x (******) y (*)*) z

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "u"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "v"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier "w"
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Identifier "y"
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Identifier "z"
*)
