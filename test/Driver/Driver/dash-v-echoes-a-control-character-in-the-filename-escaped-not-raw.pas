(*
Passing -v echoes each stage's command line to stderr straight from Args,
which includes the input filename verbatim (see makeFEArgs); Driver::runTool
used to write that echo with no escaping -- the same hole as an unescaped
filename in a diagnostic's "file:line:col:" prefix, just reachable without a
compile error to trigger it.

Each argument is control-char-escaped (issue #281) and then quoted the way
clang's own -### output is, via llvm::sys::printArg (issue #286), which
backslash-escapes any embedded '\' -- so the single '\' that introduces the
\x1b escape below is itself doubled in the echoed line.

RUN: split-file %s %t.dir
RUN: %plang -v -c "%t.dir/ev[31mRED.pas" -o %t.dir/out.o > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK: ev\\x1b[31mRED.pas
CHECK-NOT: 
*)

//--- ev[31mRED.pas
program p(output); begin writeln('ok') end.
