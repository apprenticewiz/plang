(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

thisisanunusuallylongidentifierbutitislegal

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "thisisanunusuallylongidentifierbutitislegal"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
