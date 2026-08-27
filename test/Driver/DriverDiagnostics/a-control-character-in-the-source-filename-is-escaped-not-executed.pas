(*
A diagnostic's "file:line:col:" prefix used to carry the source filename
straight from argv into DiagnosticPrinter::printHeadline with no escaping,
so a name holding an SGR escape rewrote the diagnostic's own color and a
name holding a bare newline split one diagnostic across two lines -- log
injection, a fake line a tailed build log would read as its own. Paired
with DiagnosticLanguage's control-character test, which covers the other
unescaped source: a locale tag reaching --version.

RUN: split-file %s %t.dir
RUN: not %plang -c "%t.dir/ev[31mRED.pas" -o %t.dir/out.o 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: ev\x1b[31mRED.pas:1:18: error:
CHECK-NOT: 
*)

//--- ev[31mRED.pas
program p; begin x := 1 end.
