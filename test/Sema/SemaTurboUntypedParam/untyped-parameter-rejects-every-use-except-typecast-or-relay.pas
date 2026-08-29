(*
Turbo's untyped parameter (procedure P(var x), no ': type' at all) may be
used in exactly two ways: as the operand of a variable typecast (its whole
reason to exist -- see untyped-parameter-fillchar-idiom-zeroes-memory.pas,
CodeGen/Turbo), and relayed straight through, with no typecast, to another
untyped formal (confirmed against a local fpc -Mtp build).  Everything
else -- arithmetic, indexing, field access, an ordinary read -- has to be
rejected with a real diagnostic.  Sym->Ty is deliberately null for one
(ParamGroup::Type / Type::Param::IsUntyped, Sema::checkIdent's own
comment); a naive implementation either crashed the first time something
dereferenced that null Ty, or silently substituted the generic TyErr
sentinel and let every one of these through unchecked (TyErr suppresses
mismatch diagnostics everywhere else in this compiler, on purpose, to avoid
cascades) -- checkIdent gives its own diagnostic instead, before either of
those can happen.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure Bad(var x);
var i: Integer;
begin
  i := x + 1;
  x[0] := 1;
  x.f := 1;
  writeln(x);
end;
begin
end.

(*
CHECK: untyped parameter 'x' can only be used through a type cast, or passed on to another untyped parameter
CHECK: untyped parameter 'x' can only be used through a type cast, or passed on to another untyped parameter
CHECK: untyped parameter 'x' can only be used through a type cast, or passed on to another untyped parameter
CHECK: untyped parameter 'x' can only be used through a type cast, or passed on to another untyped parameter
*)
