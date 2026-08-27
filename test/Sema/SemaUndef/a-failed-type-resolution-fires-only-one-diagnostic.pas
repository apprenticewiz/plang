(*
Sema's Phase 3a (checkBlock, Sema.cpp) gives every type name in a
type-definition-part a per-type stub -- Kind=Error, Name set to the type's
own name -- so that resolveNamed (SemaType.cpp) can recognize "not yet
resolved by Phase 3b" and, outside a pointer's domain, diagnose it as a
forward reference (ISO Sec6.2.2.9, EP Sec6.2.1(k)).  Phase 3b then
overwrites the stub with whatever resolveType actually returns -- and when
the type's own denoter fails to resolve (nosuchtype below), that is the
shared TyErr singleton, whose Name is the fixed, non-empty "<error>".
resolveNamed's stub check could not tell TyErr apart from a still-pending
stub, so the one real "undefined type" diagnostic fanned out into a bogus
"used here before its declaration" at every later use of the broken type
(issue #269).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: undefined type 'nosuchtype'
CHECK-NOT: used here before its declaration
COUNT:1
*)

program p;
type q = nosuchtype;
var v: q;
var w: q;
begin end.
