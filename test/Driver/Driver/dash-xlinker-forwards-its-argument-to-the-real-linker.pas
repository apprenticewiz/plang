(*
-Xlinker <arg> is the standard gcc/clang way to forward a single raw
argument straight to the linker, unsplit (unlike -Wl,, which commas
apart).  This driver invokes the real linker directly rather than
through gcc, so the collected -Xlinker argument has to actually land in
the real link command's own argument list, with the "-Xlinker" marker
itself dropped -- the linker does not understand a literal "-Xlinker"
argument and rejects it outright as unknown if it is ever handed
through verbatim.

Proving the forwarding happened needs a check that holds on every
linker this project's CI actually uses (ld.lld on Linux, Apple's own ld
on macOS) -- an intentionally nonexistent flag does this portably: every
real linker rejects it by name, so the flag's own spelling showing up in
the link error is direct evidence it reached the linker rather than
being silently dropped (the pre-fix bug). An earlier version of this
test used -Xlinker -Map=<file> for an observable successful side effect
instead, but -Map is not portable: ld.lld accepts it, Apple's ld does
not.
*)

(*
RUN: not %plang %s -o %t -Xlinker --plang-test-nonexistent-xlinker-flag-yyxx 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p(output);
begin writeln('hi') end.

(*
CHECK: plang-test-nonexistent-xlinker-flag-yyxx
*)
