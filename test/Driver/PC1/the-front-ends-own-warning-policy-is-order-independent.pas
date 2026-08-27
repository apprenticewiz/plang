(*
Issue #276's fix reads -w/-Werror/-Wno-<name> in a prescan over the whole
command line before reporting anything, the same way
Driver::configureDiagnostics does for the driver -- so a policy flag still
applies to a diagnostic that named the flag it depends on earlier on the
same command line, not just a later one.  This checks -Wbogus-warning-name
before -w (the opposite order from the other PC1 tests here) still gets
silenced, and the same diagnostic before -Werror (opposite order again)
still turns fatal.
*)

(*
RUN: %plang_ir -pc1 -Wbogus-warning-name -w %s -o %t.silenced.ir > %t.silenced.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=SILENCED-ABSENT %s < %t.silenced.out
RUN: not %plang_ir -pc1 -Wbogus-warning-name -Werror %s -o %t.fatal.ir > %t.fatal.out 2>&1
RUN: FileCheck --check-prefix=FATAL %s < %t.fatal.out
*)

(*
SILENCED-ABSENT-NOT: unknown warning
FATAL: error: unknown warning '-Wbogus-warning-name'
*)

program p;
begin end.
