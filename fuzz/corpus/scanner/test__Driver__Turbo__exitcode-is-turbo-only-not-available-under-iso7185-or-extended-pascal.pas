(*
Regression gate: ExitCode is only registered by Sema::registerBuiltins
under Opts.turbo(), so under -std=iso7185 or -std=iso10206 it is not a
required identifier refused by name (the way a TP-only Builtins.def entry
like RunError/Assert is, with err_turbo_required_name's wording) -- it is
simply never declared at all, so a program that names it gets the same
"undefined identifier" any other misspelled or never-declared name would.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: undefined identifier 'ExitCode'
*)

program p;
begin ExitCode := 5 end.
