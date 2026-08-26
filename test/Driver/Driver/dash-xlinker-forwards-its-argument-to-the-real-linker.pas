(*
-Xlinker <arg> is the standard gcc/clang way to forward a single raw
argument straight to the linker, unsplit (unlike -Wl,, which commas
apart).  This driver invokes ld.lld directly rather than through gcc, so
the collected -Xlinker argument has to actually land in the real link
command's own argument list, with the "-Xlinker" marker itself dropped
-- ld.lld does not understand a literal "-Xlinker" argument and rejects
it outright as unknown if it is ever handed through verbatim.
-Xlinker -Map=<file> is used here because it has an observable,
checkable side effect: a linker map file the real linker itself writes
out.
*)

(*
RUN: %plang %s -o %t -Xlinker -Map=%t.map
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines --check-prefix=OUT %s
RUN: FileCheck --check-prefix=MAP %s < %t.map
*)

program p(output);
begin writeln('hi') end.

(*
OUT:hi
*)

(*
MAP: VMA LMA Size Align Out In Symbol
MAP: .interp
*)
