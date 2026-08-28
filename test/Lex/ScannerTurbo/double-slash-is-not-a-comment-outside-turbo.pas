(*
Turbo's '//' line comment (line-comment-runs-to-end-of-line-under-turbo.pas,
right next door) is Turbo's own addition -- ISO 7185 and Extended Pascal
have no such thing, so two slashes there are just two Divide tokens in a
row, not a comment.

RUN: %plang_ir -dump-tokens %s | FileCheck --check-prefix=ISO %s
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck --check-prefix=EP %s
*)

x // y

(*
ISO: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
ISO-NEXT: [[P2:[0-9]+:[0-9]+]]: Divide
ISO-NEXT: [[P3:[0-9]+:[0-9]+]]: Divide
ISO-NEXT: [[P4:[0-9]+:[0-9]+]]: Identifier "y"

EP: [[Q1:[0-9]+:[0-9]+]]: Identifier "x"
EP-NEXT: [[Q2:[0-9]+:[0-9]+]]: Divide
EP-NEXT: [[Q3:[0-9]+:[0-9]+]]: Divide
EP-NEXT: [[Q4:[0-9]+:[0-9]+]]: Identifier "y"
*)
