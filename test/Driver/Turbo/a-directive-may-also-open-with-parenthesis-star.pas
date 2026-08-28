(*
A Turbo compiler directive is either comment opener immediately followed
by a dollar sign (no gap) -- not brace-comment syntax specifically, even
though the family this file's siblings exercise is always written that
way. Checked against real fpc in TP-compatibility mode before writing
this: a star-paren-opened directive is recognized exactly the same as a
brace-opened one there, both reported as the same Note. plang's own
skipDirective (lib/Lex/Directives.cpp) closes each the way skipCommentTurbo
already closes an ordinary Turbo comment of that kind -- a same-kind
terminator only, so the opener used below needs its own matching closer,
not the other family's -- and lower-case directive and argument text both
work, since the directive name is folded for lookup the same way an
identifier is.

All of that is deliberately described above in words rather than spelled
out with the real two-character sequences: writing this file's own
two-character directive opener followed later by its own two-character
closer, right here inside plang's top-of-file documentation comment, would
close THIS comment early under -std=turbo's own same-kind-terminator rule
-- exactly the mechanism the program below exercises on purpose, not by
accident.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=PSNOTE %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
PSNOTE: note: paren style, lower case
RAN:ran
*)

program paren_style;
(*$note paren style, lower case*)
begin
  writeln('ran')
end.
