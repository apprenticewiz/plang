(*
Two processes resolving independently; if they disagreed, half a
compilation's messages would be in one language and half in the other.
Compares the driver's and the front end's own "Messages: ..." line
against EACH OTHER via a real diff, not a hardcoded expected string --
the line embeds the resolved catalog's absolute install path, which is
build-tree-specific and would make a pinned literal false on every
machine except the one it was captured on (found for real: this file's
first draft hardcoded a local dev path and failed on every CI runner,
each with its own different checkout path).

RUN: %plang_ir -fdiagnostics-language=qps_ploc --version > %t.driver.out 2>&1; true
RUN: %plang_ir -pc1 -fdiagnostics-language=qps_ploc --version > %t.frontend.out 2>&1; true
RUN: grep 'Messages: ' %t.driver.out > %t.driver.line
RUN: grep 'Messages: ' %t.frontend.out > %t.frontend.line
RUN: FileCheck --check-prefix=NONEMPTY %s < %t.driver.line
RUN: diff %t.driver.line %t.frontend.line
*)

(*
NONEMPTY: Messages:
*)
