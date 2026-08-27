(*
Issue #277: in -c (or -S/-emit-llvm/one of the -dump-* modes), compile()
returns before ever reaching the link step, so a precompiled .o/.a on the
command line -- which parseArgs had already routed into Opts.linkerArgs on
the assumption *something* downstream would consume it -- was silently
dropped without a word, not even checked for existing:
"plang -c ok.pas /usr/lib64/Scrt1.o -o ok.o" ran clean, exit 0.  gcc's own
driver warns for exactly this ("linker input file unused because linking
not done"); this is plang's equivalent, and -- like every other plang
driver diagnostic -- a real -W-named warning (off with -w, fatal with
-Werror, listed by --help-warnings), not a hardcoded stderr print.

/dev/null stands in for a real .pas file throughout, the same trick
an-option-missing-its-value-is-reported-not-asserted-o.pas and its
neighbors in this directory already use for driver-argument-parsing checks
that have nothing to do with what the front end makes of the input: none of
these RUN lines actually need compile() to succeed, or even run (the WERROR
one proves compile() does not run at all in that case -- no front-end
parse diagnostic for /dev/null appears alongside the driver's own single
line, which it would if compile() had gone ahead and tried).
*)

(*
RUN: %plang_ir -c /dev/null /usr/lib64/Scrt1.o -o %t.o > %t.default.out 2>&1; true
RUN: FileCheck --check-prefix=DEFAULT %s < %t.default.out

RUN: %plang_ir -c /dev/null /no/such/file.o -o %t.o > %t.missing.out 2>&1; true
RUN: FileCheck --check-prefix=MISSING %s < %t.missing.out

RUN: %plang_ir -c /dev/null -lm -o %t.o > %t.dashl.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=DASHL %s < %t.dashl.out

RUN: %plang_ir /usr/lib64/Scrt1.o -o %t.linked > %t.link.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=DASHL %s < %t.link.out

RUN: not %plang_ir -c /dev/null /usr/lib64/Scrt1.o -Werror -o %t.o > %t.werror.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines --check-prefix=WERROR %s < %t.werror.out

RUN: %plang_ir --help-warnings > %t.hw.out 2>&1
RUN: FileCheck --check-prefix=HELPW %s < %t.hw.out
*)

(*
DEFAULT: warning: linker input file '/usr/lib64/Scrt1.o' unused because linking is not done in this mode

MISSING: warning: linker input file '/no/such/file.o' unused because linking is not done in this mode

DASHL-NOT: linker input file

WERROR:plang: error: linker input file '/usr/lib64/Scrt1.o' unused because linking is not done in this mode

HELPW: -Wno-linker-input-unused
*)
