(*
Regression gate: ParamCount/ParamStr are TP-only Builtins.def entries
(Dialects = TP, like RunError/GetMem -- see
getmem-is-turbo-only-not-available-under-iso7185-or-extended-pascal.pas,
this file's own direct model), so under -std=iso7185 or -std=iso10206 they
must be refused with err_turbo_required_name's wording, not merely be
undefined names.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'paramcount' is a Turbo Pascal extension and is only available under -std=turbo
*)

program p;
begin writeln(ParamCount) end.
