(*
-Wl,<args> is the standard gcc/clang way to forward raw flags straight to
the linker, splitting on commas the way gcc/clang themselves do.  This
driver invokes the real linker directly rather than through gcc, so the
collected -Wl, arguments have to be split out of the literal "-Wl,..."
spelling and appended to the real link command's own argument list -- the
linker does not understand "-Wl," itself (that syntax only means
something to a gcc/clang front end sitting in front of a linker) and
rejects it outright as an unknown argument if it is ever handed through
verbatim.

Proving the forwarding happened needs a check that holds on every linker
this project's CI actually uses (ld.lld on Linux, Apple's own ld on
macOS) -- an intentionally nonexistent flag does this portably: every
real linker rejects it by name, so the flag's own spelling showing up in
the link error is direct evidence it reached the linker rather than
being silently dropped (the pre-fix bug), regardless of each linker's
own wording for "unknown option".  An earlier version of this test used
-Wl,-Map,<file> for an observable successful side effect instead, but
-Map is not portable: ld.lld accepts it, Apple's ld does not.
*)

(*
RUN: not %plang %s -o %t -Wl,--plang-test-nonexistent-flag-zzqq,another-bogus-arg-wwrr 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p(output);
begin writeln('hi') end.

(*
CHECK: plang-test-nonexistent-flag-zzqq
*)
