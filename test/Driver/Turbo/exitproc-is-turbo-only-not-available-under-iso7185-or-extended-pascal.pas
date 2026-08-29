(*
Regression gate: ExitProc, like ExitCode (exitcode-is-turbo-only-not-
available-under-iso7185-or-extended-pascal.pas, this file's own direct
model), is registered by Sema::registerBuiltins only under Opts.turbo(), so
under -std=iso7185 or -std=iso10206 it is never declared at all -- an
ordinary "undefined identifier", not a Turbo-extension-refused name.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: undefined identifier 'ExitProc'
*)

program p;
var b: Boolean;
begin b := (ExitProc = nil) end.
