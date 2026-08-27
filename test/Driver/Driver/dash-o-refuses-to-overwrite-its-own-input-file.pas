(*
Issue #148: plang silently destroyed its own .pas source file when -o named
the same path as the input.  The driver never checked whether the resolved
output path matched the resolved input path before running the pipeline, so
"plang hello.pas -o hello.pas" ran the front end, then llc, then the linker,
and the final write step truncated hello.pas out from under itself -- for
both the default executable-output mode and -c (object) mode.

Checked on resolved (canonical) paths, not literal spellings, so
"hello.pas" and "./hello.pas" -- the same file, spelled differently -- are
caught too, not just a byte-identical -o argument.

Also checks that an ordinary invocation, where -o names a genuinely
different path, is completely unaffected.
*)

(*
RUN: split-file %s %t.dir

RUN: not %plang %t.dir/hello.pas -o %t.dir/hello.pas 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=SRC --strict-whitespace --match-full-lines %s < %t.dir/hello.pas

RUN: not %plang -c %t.dir/hello.pas -o %t.dir/hello.pas 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=SRC --strict-whitespace --match-full-lines %s < %t.dir/hello.pas

RUN: not %plang %t.dir/hello.pas -o %t.dir/./hello.pas 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=SRC --strict-whitespace --match-full-lines %s < %t.dir/hello.pas

RUN: %plang %t.dir/hello.pas -o %t.dir/hello_bin
RUN: %run %t.dir/hello_bin | FileCheck --check-prefix=OUT --strict-whitespace --match-full-lines %s
*)

(*
ERR: is the same as output file
*)

(*
OUT:hi
*)

(*
SRC:program hello;
SRC:begin
SRC:  writeln('hi');
SRC:end.
*)

//--- hello.pas
program hello;
begin
  writeln('hi');
end.
