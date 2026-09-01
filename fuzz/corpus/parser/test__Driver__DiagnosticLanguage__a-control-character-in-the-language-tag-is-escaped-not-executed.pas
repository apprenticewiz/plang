(*
The language tag comes from $LANG/$LC_MESSAGES or -fdiagnostics-language=,
none of which plang chose; describeLocale used to hand it to --version raw.
A tag holding a clear-screen-and-home escape used to repaint the terminal
the moment --version ran it -- escapeControlChars (StringUtil.h) now turns
every C0 byte and DEL into a visible \xHH escape before it reaches
"Messages:", the same guard DiagnosticPrinter applies to a source filename.
Quoted on the RUN line: the tag's own ";" would otherwise end the shell
command right there, which is a second, independent way an unescaped
control-derived string can misbehave even before it reaches plang's output.

RUN: %plang_ir "-fdiagnostics-language=fr[2J[1;1HHIJACKED" --version > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK: Messages: fr\x1b[2J\x1b[1;1HHIJACKED (no catalog found; using built-in en_US)
CHECK-NOT: 
*)
