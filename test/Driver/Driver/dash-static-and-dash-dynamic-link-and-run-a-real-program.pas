(*
Issue #805: findRuntimeLib hardcoded the literal "lib" when building the
installed-layout candidate path to the runtime archive, but the install
rules (top-level CMakeLists.txt) put it at CMAKE_INSTALL_LIBDIR, which
GNUInstallDirs resolves to "lib64" (not "lib") on Fedora, openSUSE, and
other 64-bit multi-lib distros. On such a system the driver silently found
no runtime at all and linked without it, producing a raw, driver-
diagnostic-free `undefined symbol: plang_set_args` et al. from the linker --
exactly what the issue's own reporter hit. The fix bakes the real
CMAKE_INSTALL_LIBDIR value in as PLANG_INSTALL_LIBDIR (lib/Driver/
CMakeLists.txt) and looks there instead.

The same issue also asked for -static/-dynamic driver flags: before this,
the driver only ever knew how to auto-link libplang.a (embedding its own
path directly in the link command); there was no dynamic auto-link path at
all, so a user who wanted one had to pass -lplang by hand, which only
happened to work because the *build* host's linker default search path
included libplang.so. -dynamic is now the default (-L<libdir> -lplang plus
a matching -rpath, so the produced binary can find libplang.so/.dylib again
at run time without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH), and -static opts
back into the old self-contained-binary behavior.

This test cannot reproduce the lib64-specific half directly (lit always
runs the in-tree build-dir binary, whose own PLANG_RUNTIME_DIR fallback
finds the runtime regardless of CMAKE_INSTALL_LIBDIR); that half was
verified by hand against a real `cmake --install` done with
-DCMAKE_INSTALL_LIBDIR=lib64. What this test DOES cover, and lit can:
exercising the whole pipeline for real -- compile, link, AND RUN the
produced binary, for both -static and the default dynamic case -- since
this bug's whole nature was a successful *compile* that only failed at
actual *execution*; a check that only asked whether the driver invocation
itself reported success would not have caught it. It also pins the -v
output shape for each mode, so a future regression that silently drops
-rpath (leaving a dynamic binary that only runs by accident, the same way
the original bug did) fails loudly here instead.
*)

(*
RUN: %plang -v %s -o %t.dynamic > %t.dynamic.log 2>&1
RUN: FileCheck --check-prefix=DYNAMIC-CMD %s < %t.dynamic.log
RUN: %run %t.dynamic | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -dynamic -v %s -o %t.explicit-dynamic > %t.explicit-dynamic.log 2>&1
RUN: FileCheck --check-prefix=DYNAMIC-CMD %s < %t.explicit-dynamic.log
RUN: %run %t.explicit-dynamic | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -static -v %s -o %t.static > %t.static.log 2>&1
RUN: FileCheck --check-prefix=STATIC-CMD %s < %t.static.log
RUN: %run %t.static | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -static -dynamic -v %s -o %t.last-wins > %t.last-wins.log 2>&1
RUN: FileCheck --check-prefix=DYNAMIC-CMD %s < %t.last-wins.log
*)

program hello(output);
begin
  writeln('hello world')
end.

(*
DYNAMIC-CMD: "-lplang"
DYNAMIC-CMD: "-rpath"

STATIC-CMD: libplang.a"
STATIC-CMD-NOT: "-lplang"

RAN:hello world
*)
