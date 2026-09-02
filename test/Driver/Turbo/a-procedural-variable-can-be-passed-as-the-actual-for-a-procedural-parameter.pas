(*
Issue #647: a procedural VARIABLE (already holding a routine) could not
itself be passed as the actual argument for a procedural-typed PARAMETER
-- 'RunIt(p, 42)' where p: TProc was refused with "the argument must be
the name of a procedure or function", even though p's own declared type
is exactly the formal's type.  Only a bare routine NAME (checkRoutineValue's
own RoutineReference path) was accepted; a variable that already held one
was not.  fpc -Mtp accepts and runs this.

Sema::checkProcedureActual now also accepts a Var/VarParam symbol whose own
declared type is callable, congruity-checked the identical way a routine
name's own signature is.  CodeGen (ClosureAndCallABI::pushProcParamArgs /
procVarRelayThunk) wraps the variable's own flat entry-point pointer
through a thunk shared by every procedural-variable actual of a given
signature -- there is no single compile-time target to build a dedicated
thunk for, unlike a bare routine name.

Two calls, through two DIFFERENT procedural variables holding two
different routines, confirm the variable's CURRENT value travels through
correctly rather than some fixed or stale target.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:first:42
CHECK-NEXT:second:99
*)

program ProcVarAsProcParamActual;

type
  TProc = procedure(v: Integer);

var
  p: TProc;

procedure First(v: Integer);
begin
  Writeln('first:', v);
end;

procedure Second(v: Integer);
begin
  Writeln('second:', v);
end;

procedure RunIt(cb: TProc; v: Integer);
begin
  cb(v);
end;

begin
  p := First;
  RunIt(p, 42);
  p := Second;
  RunIt(p, 99);
end.
