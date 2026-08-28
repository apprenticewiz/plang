(*
The regression gate for the I/INCLUDE source-inclusion directive
specifically, alongside this directory's existing
iso7185-and-extended-pascal-still-treat-dollar-brace-as-an-ordinary-
comment.pas (the message-directive family) and
...-still-ignore-ifdef-like-comment-text.pas (conditional compilation):
dispatchIncludeDirective is only ever reached from dispatchDirective, itself
only ever reached from skipDirective, itself only ever called when
Opts.turbo() -- see skipWhitespaceAndComments in Scanner.cpp.  Under
-std=iso7185 (the default) and -std=iso10206 a brace comment that happens to
spell out an I directive naming a file is still just an ordinary, silently
ignored comment: no file is ever opened, no diagnostic is ever raised for
the file this names not existing (it does not, anywhere on disk), and the
program compiles and runs exactly as if the comment had any other text in
it.  (No literal curly braces in this paragraph, deliberately: ISO 7185
Sec6.1.8's "either terminator ends either" rule means a bare closing one in
here would end this VERY comment block early -- see tools/lint_test.py's
check 1, and the sibling ...-still-ignore-ifdef-like-comment-text.pas
header, which avoids the same hazard the same way.)
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

program dollarinclude;
{$I this-file-does-not-exist-anywhere-on-disk.inc}
begin
  writeln('ran')
end.
