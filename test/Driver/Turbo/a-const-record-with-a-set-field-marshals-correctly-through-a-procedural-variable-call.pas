(*
Issue #772, the segfault variant: a `const` RECORD parameter with a SET
field, called indirectly through a procedural VARIABLE's own value.  Before
the fix, ClosureAndCallABI::procVarFnType declared the indirect call's LLVM
signature as a bare struct (matching neither the direct call's own pointer
parameter nor the actual argument's real address), and emitProcVarCall's
argument-marshalling loop still passed byRef=false unconditionally -- wide
enough to read past the caller's own stack frame for a record this size,
segfaulting.  Both the direct call and the indirect call through f have to
return the same, correct answer -- checked against a local `fpc -Mtp` build.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:direct=TRUE
CHECK-NEXT:indirect=TRUE
*)

program ConstRecordWithSetThroughProcVar;

type
  TFlagged = record
    Flags: set of 0..31;
    X, Y: Integer;
  end;
  TPred = function(const r: TFlagged): Boolean;

function HasFlag0(const r: TFlagged): Boolean;
begin
  HasFlag0 := 0 in r.Flags;
end;

var
  f: TPred;
  rr: TFlagged;

begin
  rr.Flags := [0, 3];
  rr.X := 1;
  rr.Y := 2;
  write('direct=');
  if HasFlag0(rr) then writeln('TRUE') else writeln('FALSE');
  f := HasFlag0;
  write('indirect=');
  if f(rr) then writeln('TRUE') else writeln('FALSE');
end.
