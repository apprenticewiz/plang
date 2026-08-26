(*
The same word is a plain Identifier under the default dialect and the
Otherwise keyword (EP section 6.9.3.5, a case statement's default arm)
under Extended Pascal -- the exact dialect-gating shape test-lit/Lex/'s
real ScannerEP migration will need.  Underscore-free on purpose: an EP
keyword spelled with an underscore (and_then, or_else) is itself an
EP-only extension under the default dialect, which would make this word
an outright scan error there rather than a plain identifier.

RUN: %plang_ir -dump-tokens %s | FileCheck --check-prefix=ISO %s
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck --check-prefix=EP %s
*)

program p;
begin x := otherwise end.

(*
ISO: Identifier "otherwise"
EP: Otherwise "otherwise"
*)
