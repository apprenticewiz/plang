(*
-Wl,<args> is the standard gcc/clang way to forward raw flags straight to
the linker, splitting on commas the way gcc/clang themselves do.  This
driver invokes ld.lld directly rather than through gcc, so the collected
-Wl, arguments have to be split out of the literal "-Wl,..." spelling
and appended to the real link command's own argument list -- ld.lld does
not understand "-Wl," (that syntax only means something to a gcc/clang
front end sitting in front of a linker) and rejects it outright as an
unknown argument if it is ever handed through verbatim.  -Wl,-Map,<file>
is used here (rather than -Wl,-Map=<file>) so this test also exercises
the comma split itself -- "-Map" and the map file path must land as two
separate argv entries -- and because it has an observable, checkable
side effect: a linker map file the real linker itself writes out.
*)

(*
RUN: %plang %s -o %t -Wl,-Map,%t.map
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
