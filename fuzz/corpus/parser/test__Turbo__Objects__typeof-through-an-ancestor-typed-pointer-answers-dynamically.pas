(*
Turbo Tier 5 capstone (Cluster C item 9) found this as a real bug WHILE
WRITING this capstone's own roster scenario (this directory's main test,
which needed to ask "which concrete type does this ancestor-typed pointer
actually point to" -- the textbook use for TypeOf/ClassType-style runtime
type identification, and the one item 7's own precedent tests never
actually tried). Pinned as a known gap there (this file's own former
self, typeof-through-an-ancestor-typed-pointer-answers-statically-not-
dynamically-known-gap.pas) and filed as issue #508; fixed for real here.

CGFuncCall.cpp's own TypeOf lowering used to pick the VMT from
'Args[0]->ResolvedType', Sema's STATIC type for the argument expression,
by explicit (but wrong) design -- indistinguishable from a correct,
DYNAMIC answer for every case item 7's own tests and this directory's own
typeof-distinguishes-directly-typed-instances-across-a-three-level-
hierarchy.pas exercise, because in all of them a variable's own static
type and its own dynamic type are the same thing. It stopped agreeing
with real Turbo Pascal / `fpc -Mtp` field practice the moment the two
diverge: a pointer whose declared pointee type is an ANCESTOR, holding
the address of a DESCENDANT instance -- exactly the shape virtual
dispatch itself (P^.Describe, already proven correct by item 5 and
reused throughout this whole tier) is built to handle correctly, and
now TypeOf is too: it reads the operand's own '_vptr' field at run
time, the SAME GEP-and-load ordinary virtual dispatch already uses
(Types.vptrOffsetOf), instead of resolving the argument's static type.

Below, P: PEmployee actually points at a live TExecutive (P^.Describe
dispatches to TExecutive's own override correctly, first line of output
-- proving the object's real runtime type well before TypeOf is asked
about it at all). Confirmed directly below, via a side-by-side
mode-tp-compatible build of this identical program, that plang now
matches real `fpc -Mtp`, which reads TExecutive's own instance through
its '_vptr' field and answers "yes, this is a TExecutive, no, it is not
a plain TEmployee":

    $ fpc -Mtp typeof_gap_repro.pas && ./typeof_gap_repro
    executive
    P^ correctly identified as TExecutive
    P^ correctly NOT identified as plain TEmployee

    $ plang -std=turbo typeof_gap_repro.pas -o t && ./t
    executive
    P^ correctly identified as TExecutive
    P^ correctly NOT identified as plain TEmployee

See docs/turbo.md's `TypeOf` entry (its former "Known gaps" listing for
this is gone -- there is no longer a gap to document).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program TypeOfThroughAncestorPointer;

type
  PEmployee = ^TEmployee;
  TEmployee = object
    constructor Init;
    procedure Describe; virtual;
  end;
  TExecutive = object(TEmployee)
    procedure Describe; virtual;
  end;

constructor TEmployee.Init; begin end;
procedure TEmployee.Describe; begin writeln('employee'); end;
procedure TExecutive.Describe; begin writeln('executive'); end;

var
  X: TExecutive;
  P: PEmployee;
begin
  X.Init;
  P := @X;
  P^.Describe;   { correct: dispatches to TExecutive.Describe, proving P really points at a TExecutive }

  if TypeOf(P^) = TypeOf(TExecutive) then
    writeln('P^ correctly identified as TExecutive')
  else
    writeln('P^ NOT identified as TExecutive');

  if TypeOf(P^) = TypeOf(TEmployee) then
    writeln('P^ (incorrectly) identified as plain TEmployee')
  else
    writeln('P^ correctly NOT identified as plain TEmployee');
end.

(*
CHECK:executive
CHECK-NEXT:P^ correctly identified as TExecutive
CHECK-NEXT:P^ correctly NOT identified as plain TEmployee
*)
