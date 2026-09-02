(*
Issue #611: "-c" (compile-only, do not link) with no .pas input at all -- a
linker-only invocation like "plang -c first.o -o result" -- still invoked
the linker and produced an executable.  Driver::compile()'s linker-only
branch (Opts.inputFile.empty(), reached when parseArgs routed every
argument into Opts.linkerArgs) called link() unconditionally, never
checking Opts.mode -- the mode check every other non-linking path (-c, -S,
-emit-llvm, -dump-*) already gets when a real .pas input is also present
(issue #277's warn_linker_input_unused).  "-c" must mean "stop after
producing an object, never invoke the linker", with or without a .pas
input on the command line.

The fix makes the linker-only branch return without linking whenever
Opts.mode is not Executable, and makes Driver::run()'s own
warn_linker_input_unused check (previously skipped whenever Opts.inputFile
was empty, on the theory that the linker-only branch always consumed the
file) fire for this case too, so the ignored .o/.a is actually reported as
unused rather than the two just silently disagreeing.

Also checks the complementary case still works: "-c" is not accidentally
disabling the perfectly ordinary link-only workflow it always supported
(no -c at all, only .o inputs) -- that keeps working exactly as
linker-only-invocation-links-precompiled-object-files.pas already checks,
just re-confirmed here alongside the new behavior for contrast.
*)

(*
RUN: rm -rf %t.dir
RUN: split-file %s %t.dir
RUN: %plang -c %t.dir/first.pas -o %t.dir/first.o

RUN: %plang -c %t.dir/first.o -o %t.dir/result > %t.default.out 2>&1
RUN: FileCheck --check-prefix=WARN %s < %t.default.out
RUN: test ! -e %t.dir/result

RUN: not %plang -c %t.dir/first.o -Werror -o %t.dir/result2 > %t.werror.out 2>&1
RUN: FileCheck --check-prefix=WERROR %s < %t.werror.out
RUN: test ! -e %t.dir/result2

RUN: %plang %t.dir/first.o -o %t.dir/still_links
RUN: %run %t.dir/still_links | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
WARN: warning: linker input file '{{.*}}first.o' unused because linking is not done in this mode
WERROR: error: linker input file '{{.*}}first.o' unused because linking is not done in this mode
CHECK:hi
*)

//--- first.pas
program first;
begin
  writeln('hi')
end.
