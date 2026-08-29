(*
The single most important check for Turbo procedural VALUES: a procedural
VARIABLE lowers as one flat pointer with no captured-frame slot at all
(unlike a procedural PARAMETER, carried as an entry-point-plus-frame pair --
see ClosureAndCallABI), so a NESTED routine -- one that may read/write its
enclosing activation's own variables through a static link -- must never be
storable in one.  Assigning it would compile cleanly and dangle the static
link the moment Outer returns, a memory-corruption bug and not merely a
type error, which is exactly why this is refused at compile time rather
than left to crash or corrupt memory at run time.  Real Turbo Pascal
disallows this outright; see Sema::checkRoutineValue's own comment for why
every nested routine is refused here, not only one proven to capture
something.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'Inner' is a nested procedure or function and cannot be used as a procedural value
*)

program p;

type
  TProc = procedure(x: integer);

var
  f: TProc;

procedure Outer;
  procedure Inner(x: integer);
  begin
    writeln(x);
  end;
begin
  f := Inner;
end;

begin
  Outer;
end.
