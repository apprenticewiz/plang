(*
The other half of Sema::checkRoutineValue's refusal, alongside a nested
routine's own (see a-nested-routine-cannot-be-assigned-to-a-procedural-
variable.pas): an ISO Sec6.6.3.1 procedural PARAMETER is itself an
entry-point-plus-frame pair received at run time, and what routine it is
actually bound to -- possibly a nested, capturing one from some entirely
different activation -- cannot be known here.  Storing just its entry
point into a procedural variable's flat pointer would silently drop that
frame, so this is refused outright the same way a directly-named nested
routine is, rather than only when the actual turns out to capture
something.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'cb' is a procedural parameter and cannot be used as a procedural value
*)

program p;

type
  TProc = procedure(x: integer);

var
  f: TProc;

procedure UseIt(procedure cb(x: integer));
begin
  f := cb;
end;

procedure DoIt(x: integer);
begin
  writeln(x);
end;

begin
  UseIt(DoIt);
end.
