(*
Issue #636: comparing two STRING LITERALS (or a Char literal against a
multi-char one) under -std=turbo used to fall through to EP's space-padded
comparison runtime (plang_str_eq and siblings) instead of ShortString's own
prefix/no-pad rules (plang_sstr_eq and siblings) -- the ShortString gate in
CGBinaryOps.cpp checked ExprIsShortStr on each operand, which a LITERAL
never is (Sema types a multi-character literal Kind::String, same as ISO
7185's plain string-type, even under Turbo -- SemaExpr.cpp's checkExpr):
there was no ShortString-typed operand anywhere in sight for a purely
literal-vs-literal (or Char-vs-literal) comparison, even though Turbo has
no EP string semantics to fall back on at all.  'a' = 'a ' is FALSE under
Turbo (shorter is strictly less, never padded to compare equal) --
confirmed against fpc -Mtp, and already plang's own documented behavior for
a ShortString VARIABLE (see shortstring-comparison-is-prefix-lexicographic-
shorter-is-less-contrast-with-ep.pas, right next to this file) -- but a
LITERAL used to get EP's answer (TRUE) instead.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:FALSE
CHECK-NEXT:TRUE
CHECK-NEXT:TRUE
CHECK-NEXT:FALSE
*)

begin
  writeln('a' = 'a ');
  writeln('a' < 'a ');
  writeln('a' <> 'a ');
  writeln('a ' <= 'a');
end.
