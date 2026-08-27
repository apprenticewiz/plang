(*
Issue #277's own repro, end to end, with real files rather than the
/dev/null stand-in
DriverDiagnostics/compile-only-mode-names-a-linker-only-input-it-will-not-use.pas
uses to check the diagnostic itself in isolation: a real, already-compiled
.o named alongside -c.  The new warning must not change what -c actually
does -- the real .pas input still compiles, and the object file it
produces is still valid and linkable -- and -Werror must genuinely stop the
compile rather than just print "error:" and carry on anyway.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -c %t.dir/other.pas -o %t.dir/other.o

RUN: %plang -c %t.dir/ok.pas %t.dir/other.o -o %t.dir/ok.o > %t.warn.out 2>&1
RUN: FileCheck --check-prefix=WARN %s < %t.warn.out
RUN: %plang %t.dir/ok.o -o %t.dir/ok_bin
RUN: %run %t.dir/ok_bin | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: not %plang -c %t.dir/ok.pas %t.dir/other.o -Werror -o %t.dir/werror.o > %t.werror.out 2>&1
RUN: FileCheck --check-prefix=WERROR %s < %t.werror.out
*)

(*
WARN: warning: linker input file '{{.*}}other.o' unused because linking is not done in this mode
RAN:hi
WERROR: error: linker input file '{{.*}}other.o' unused because linking is not done in this mode
*)

//--- ok.pas
program ok;
begin
  writeln('hi')
end.

//--- other.pas
program other;
begin
  writeln('other')
end.
