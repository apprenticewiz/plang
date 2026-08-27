(*
Issue #276: without engine routing, -Werror had no effect on the front
end's own "unknown warning" message either -- it printed to std::cerr and
compilation carried on regardless, exiting 0 on an otherwise-valid program
(the driver's equivalent, "plang -Werror -O9", was already engine-routed
and exits 1).  Engine-routed, the message is now reported at Error
severity and the front end refuses to proceed, exiting 1 -- and it is
formatted the same way every other engine-routed front-end diagnostic
with no place in the source is, "error: <message>" with no program-name
prefix (like clang -cc1, and like this front end's own err_file_not_found
already was), not the ad hoc "plang -pc1: warning: ..." text this used to
print.
*)

(*
RUN: not %plang_ir -pc1 -Werror -Wbogus-warning-name %s -o %t.ir > %t.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:error: unknown warning '-Wbogus-warning-name'
*)

program p;
begin end.
