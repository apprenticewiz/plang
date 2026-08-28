(*
The one-argument form, Assert(cond), which plang_err_assert_failed
(runtime/plang_sys.cpp) reports without a trailing ": <message>" -- the
same "Assertion failed" wording `fpc -Mtp` itself uses when Assert is
called with no message (confirmed empirically).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: not %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 227: Assertion failed
*)

program assert_no_message;
begin
  Assert(false)
end.
