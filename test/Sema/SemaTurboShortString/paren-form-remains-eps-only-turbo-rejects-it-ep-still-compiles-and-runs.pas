(*
The other half of bracket-form-is-turbo-only-not-available-under-ep-or-
iso7185.pas: Extended Pascal's paren form `string(N)` (TypeKind::VarString)
must stay refused under -std=turbo exactly as it always was -- adding
Turbo's own bracket form must not accidentally widen the paren form's own
dialect gate (SemaType.cpp's `if (!Opts.extendedPascal())` check, unchanged
by this feature) to also accept Turbo.  And under EP itself, string(N) must
keep working completely unaffected: same diagnostic, same compiled output,
as before ShortString existed -- a real behavioural check (compiles AND
runs AND prints correctly), not just a compiles-without-erroring check.

RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck --check-prefix=TURBO-REJECTS %s < %t.err
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --check-prefix=EP-RUNS --strict-whitespace --match-full-lines %s
*)

program p(output);
var s: string(10);
begin
  s := 'hi';
  writeln(s)
end.

(*
TURBO-REJECTS: error: the type 'string(n)' is an Extended Pascal extension and is not available under -std=iso7185
EP-RUNS:hi
*)
