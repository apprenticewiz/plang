(*
The other direction from ep-conformant-array-syntax-is-rejected-under-
turbo-ep-still-accepts-it.pas: Turbo's own open-array parameter form
(array of T, no bracket at all) never existed before this dialect and must
stay rejected under both ISO 7185 and Extended Pascal, each of which parses
a bare 'array' only through parseConformantOrRegular -- which always
expects '[' immediately after 'array' and so fails with an ordinary parse
error, not a bespoke diagnostic, for Turbo's bracket-less form.  Turbo
itself accepts it and reaches Sema clean (parameter type resolves,
signature is well-formed) -- confirmed by running -dump-ast rather than
merely not erroring, so a change that broke resolution silently (e.g. left
the parameter's Type null the way an UNTYPED one legitimately is) would
still fail this.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: %plang -std=turbo -dump-ast %s > %t.ast
RUN: FileCheck --check-prefix=TURBO-AST %s < %t.ast
*)

program p;
function Sum(a: array of Integer): Integer;
begin
  Sum := 0;
end;

begin
end.

(*
CHECK: expected '[', got 'of'
TURBO-AST: Sum
*)
