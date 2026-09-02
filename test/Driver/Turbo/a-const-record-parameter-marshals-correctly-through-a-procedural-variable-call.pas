(*
Issue #772: calling a procedural VARIABLE that takes a `const` RECORD
parameter through the variable's own VALUE (an indirect call) used to
mis-marshal the argument -- ClosureAndCallABI::procVarFnType/emitProcVarCall
never checked Turbo's `const`-on-a-structured-type by-reference convention
(isStructuredForConstByRef, Sema/Type.h) the way a direct call
(CodeGenProcs.cpp) already did, so the callee's declared LLVM signature (a
bare struct) disagreed with a direct call's own (a pointer) for the exact
same routine.  For a small plain record like TPoint here, that silently
handed the callee garbage/misaligned bits instead of the real fields --
IsOrigin(pt) (direct) correctly returns TRUE, but f(pt) (indirect, through
the procedural variable f) used to wrongly return FALSE.  Both calls have
to agree, and both have to match real Turbo Pascal (checked against a local
`fpc -Mtp` build).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:direct=TRUE
CHECK-NEXT:indirect=TRUE
*)

program ConstRecordThroughProcVar;

type
  TPoint = record X, Y: Integer end;
  TPred = function(const p: TPoint): Boolean;

function IsOrigin(const p: TPoint): Boolean;
begin
  IsOrigin := (p.X = 0) and (p.Y = 0);
end;

var
  f: TPred;
  pt: TPoint;

begin
  pt.X := 0;
  pt.Y := 0;
  write('direct=');
  if IsOrigin(pt) then writeln('TRUE') else writeln('FALSE');
  f := IsOrigin;
  write('indirect=');
  if f(pt) then writeln('TRUE') else writeln('FALSE');
end.
