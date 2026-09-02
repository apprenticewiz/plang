(*
Issue #617: real Turbo Pascal has no procedure-local object type (confirmed
against a local fpc -Mtp build: "Local class definitions are not
allowed").  Unchecked, plang used to reach CodeGen with this shape and
crash the LLVM verifier ("Incorrect number of arguments passed to called
function") -- every method's own out-of-line body is emitted as an
ordinary, non-nested function taking only 'Self' and its declared
parameters (CodeGenProcs.cpp's method pre-pass), never a static-link frame
the way a genuinely nested PROCEDURE's own body gets one, so a method of a
procedure-local object type had no way to reach the enclosing procedure's
own scope the way its own shape implied it should be able to.  Rejected
now with a clean diagnostic instead, matching fpc's own behavior.

RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program LocalObjectType;

procedure Run;
type
  TA = object
    x: Integer;
    procedure Speak;
  end;
procedure TA.Speak;
begin
  writeln(x);
end;
var
  a: TA;
begin
  a.x := 1;
  a.Speak;
end;

begin
  Run;
end.

(*
CHECK: error: object type 'TA' may not be declared inside a procedure or function
*)
