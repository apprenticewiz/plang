(*
Regression gate: RunError is a TP-only Builtins.def entry (Dialects = TP,
like Assert -- see assert-is-turbo-only-not-available-under-iso7185-or-
extended-pascal.pas, this file's own direct model), so under -std=iso7185
or -std=iso10206 it must be refused with err_turbo_required_name's
wording, not merely be an undefined name.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'runerror' is a Turbo Pascal extension and is only available under -std=turbo
*)

program p;
begin RunError(200) end.
