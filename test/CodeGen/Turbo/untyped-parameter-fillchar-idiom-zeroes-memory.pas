(*
The classic reason Turbo's untyped parameter (procedure P(var x), no ':
type' at all -- ParamGroup::Type stays null, Type::Param::IsUntyped is set)
exists: `FillChar(TByteArray(x), N, 0)`, a variable typecast reinterpreting
whatever x's caller actually passed as raw bytes.  A naive implementation
that let Sym->Ty stay null all the way through would either crash the first
time anything asked for its type, or (worse) silently let the typecast
resolve to TyErr and cascade -- see untyped-parameter-signature-in-a-
congruity-diagnostic-does-not-crash.pas and the bare-use diagnostic test,
this feature's own Sema-side crash-prevention coverage, for the other half
of that audit.  Here the idiom just has to actually WORK end to end: compile,
run, and really zero the caller's own array (it is 'var', so this is by
reference, not a copy).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --match-full-lines %s
*)

program p;
type TByteArray = array[0 .. 9] of Byte;

procedure Zero(var x);
begin
  FillChar(TByteArray(x), 10, 0);
end;

var
  y: array[0 .. 9] of Byte;
  i: Integer;
begin
  for i := 0 to 9 do y[i] := 5;
  Zero(y);
  for i := 0 to 9 do write(y[i], ' ');
  writeln;
end.

(*
CHECK:0 0 0 0 0 0 0 0 0 0
*)
