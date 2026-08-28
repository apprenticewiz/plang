(*
Regression gate: Assert is a TP-only Builtins.def entry (Dialects = TP),
so under -std=iso7185 or -std=iso10206 it must be refused the same way
every other required name only one dialect declares is -- and, since it
is the first name that is Turbo's alone rather than Extended Pascal's,
with err_turbo_required_name's own wording, not err_ep_required_name's
(which would wrongly promise -std=iso10206 accepts it).  See
Sema::checkEPOnly's own comment.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'assert' is a Turbo Pascal extension and is only available under -std=turbo
*)

program p; var b: boolean;
begin b := true; Assert(b) end.
