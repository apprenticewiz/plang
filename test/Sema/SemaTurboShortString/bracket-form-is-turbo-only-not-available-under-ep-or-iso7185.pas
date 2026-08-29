(*
Turbo's bracket form `string[N]` (TypeKind::ShortString) is a distinct type
from Extended Pascal's paren form `string(N)` (TypeKind::VarString), with a
distinct binary layout -- see include/plang/Sema/Type.h's own comment on
ShortString.  The parser (ParseType.cpp) only recognizes the bracket form
under -std=turbo; under both other dialects `string[10]` is simply not a
type a string keyword can start, so it fails to parse the same way any other
unexpected '[' after a type-complete denoter would.  This is a parse-time
rejection (cascading follow-on errors are expected and are not what this
test cares about), not a "the type exists but is refused" diagnostic --
see paren-form-remains-eps-only-turbo-rejects-it-ep-still-compiles-and-
runs.pas for that shape, which is what EP's string(N) gets under Turbo.

RUN: not %plang -std=iso10206 -dump-ast %s 2> %t.ep.err
RUN: FileCheck %s < %t.ep.err
RUN: not %plang -std=iso7185 -dump-ast %s 2> %t.iso7185.err
RUN: FileCheck %s < %t.iso7185.err
RUN: %plang -std=turbo -dump-ast %s > %t.turbo.out
RUN: FileCheck --check-prefix=TURBO-OK %s < %t.turbo.out
*)

program p;
var s: string[10];
begin
  writeln('ok')
end.

(*
CHECK: error: expected ';', got '['
TURBO-OK: (shortstring 10)
*)
