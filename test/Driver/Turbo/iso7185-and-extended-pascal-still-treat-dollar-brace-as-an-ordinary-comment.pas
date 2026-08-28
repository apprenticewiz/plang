(*
The regression gate for lib/Lex/Directives.cpp: dollar-brace directive
recognition is gated on Opts.turbo() at the one call site that decides
between it and an ordinary comment (Scanner.cpp's skipWhitespaceAndComments),
so under -std=iso7185 (the default) and -std=iso10206 a brace comment whose
text happens to start with a dollar sign -- including one that spells a
real Turbo directive name, like MESSAGE or BOGUS below -- must stay exactly
what it always was: an ordinary, silently ignored comment. No diagnostic,
no change in what the program does, same as before this file existed.
Checked against the same program under both dialects and, for good
measure, against a directive-shaped input compiling clean under -std=turbo
too where its content genuinely is one (see the sibling message-directive
tests) -- it is only ISO 7185/Extended Pascal where the content is inert
either way.

NOTE: this comment deliberately never spells the two-character opener and
closer next to real directive text together -- doing that inside plang's
own OWN top-of-file documentation comment would make THIS comment close
early under ISO 7185's "either terminator closes either" rule (a lone
close-brace ends a star-paren comment too), which is exactly the kind of
mistake the program below exists to exercise on purpose, not by accident.
*)

(*
RUN: %plang %s -o %t.iso7185 > %t.iso7185.out 2>&1
RUN: FileCheck --allow-empty --check-prefix=QUIET %s < %t.iso7185.out
RUN: %run %t.iso7185 | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -std=iso10206 %s -o %t.ep > %t.ep.out 2>&1
RUN: FileCheck --allow-empty --check-prefix=QUIET %s < %t.ep.out
RUN: %run %t.ep | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
QUIET-NOT: note:
QUIET-NOT: warning:
QUIET-NOT: error:
RAN:ran
*)

program dollarbrace;
{$MESSAGE this would be an informational note under -std=turbo}
{$ERROR this would fail the compile under -std=turbo}
{$BOGUS this would be an unknown-directive warning under -std=turbo}
begin
  writeln('ran')
end.
