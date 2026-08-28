(*
ISO 7185 §6.8.2.2 requires a function's result to be used; Turbo's
{$X+} (its default -- CompilerSwitches.def's TurboDefault column for
Switch::ExtendedSyntax) lifts that requirement and lets a user-declared
function be called as a statement, its result simply discarded, the same
as an ordinary procedure call.  No {$X} directive appears in this file at
all: the point is that Turbo starts with it already on.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program xplususerfunc;
var callCount: integer;
function Foo: integer;
begin
  callCount := callCount + 1;
  writeln('foo called');
  Foo := callCount * 10;
end;
begin
  Foo;
  writeln('done, callCount=', callCount:1);
end.

(*
CHECK:foo called
CHECK-NEXT:done, callCount=1
*)
