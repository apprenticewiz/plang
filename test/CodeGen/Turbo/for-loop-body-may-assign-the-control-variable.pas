(*
Issue #650: ISO Sec 6.8.3.9's "threat" scan (Sema::checkForBody, wired up
unconditionally from Sema::checkFor and, for the inter-procedural case, from
Sema.cpp's Phase 6.5) rejects any assignment to a for-loop's control
variable within its own body. Turbo has no such restriction: `fpc -Mtp`
3.2.2 accepts an assignment to the control variable both directly in the
body and through a var-parameter call.

The first loop's output matches `fpc -Mtp` exactly (confirmed empirically):
1, 2, 8, 9, 10 -- five iterations, one more than "i=3 alone" would explain,
because the loop re-reads i's just-assigned value (8) on its next pass
rather than tracking progress through a hidden counter unaffected by it.
The second loop only needs to COMPILE (a var-parameter call aliasing the
control variable used to be err_for_body_var_param); its exact iteration
count is a codegen-internals question the Sema fix this test guards does
not answer, so nothing here depends on it.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
CHECK-NEXT:8
CHECK-NEXT:9
CHECK-NEXT:10
CHECK-NEXT:done
*)

program p;
var i: integer;

procedure bump(var x: integer);
begin
  x := x + 1;
end;

begin
  for i := 1 to 10 do begin
    if i = 3 then i := 8;
    writeln(i);
  end;

  for i := 1 to 3 do bump(i);
  writeln('done');
end.
