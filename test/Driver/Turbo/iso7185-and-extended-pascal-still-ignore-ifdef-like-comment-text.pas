(*
The regression gate for conditional compilation specifically, alongside
this directory's existing iso7185-and-extended-pascal-still-treat-
dollar-brace-as-an-ordinary-comment.pas (which covers the message-directive
family the same way): dispatchConditionalDirective is only ever reached
from dispatchDirective, itself only ever reached from skipDirective, itself
only ever called when Opts.turbo() -- see skipWhitespaceAndComments in
Scanner.cpp. Under -std=iso7185 (the default) and -std=iso10206 a brace
comment whose text happens to spell IFDEF/ENDIF is still just an ordinary,
silently ignored comment: no CondFrame is ever pushed, no symbol is ever
looked up, and in particular the ENDIF-less IFDEF below -- which would be
an unterminated-conditional error under -std=turbo (see the sibling
malformed-conditional-directive-nesting test) -- raises nothing at all
here, because it is never seen as a directive to begin with.
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

program dollarbraceifdef;
{$IFDEF THIS_WOULD_OPEN_A_CONDITIONAL_BLOCK_UNDER_TURBO}
{$ENDIF}
begin
  writeln('ran')
end.
