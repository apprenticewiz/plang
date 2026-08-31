(*
Turbo Tier 5, Cluster A item 0: parsing only.  Confirmed against a local
fpc -Mtp build that repeating 'virtual' on an out-of-line method body is
rejected outright ("Procedure directive VIRTUAL not allowed in
implementation section") -- the directive belongs on the in-class heading
only.  parseObjectMethodHeading (the in-class path) is the only place that
consumes a trailing 'virtual'/'abstract'; Parser::parseProcDecl (which
handles the out-of-line dotted body) never looks for either, so a
'virtual' sitting where a body is expected falls through to ordinary parse
recovery.  This does not need to reproduce fpc's own diagnostic wording --
only that the compiler does not silently accept the (real, confirmed)
Turbo Pascal error.
*)

(*
RUN: not %plang_ir -std=turbo -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program RepeatedVirtual;

type
  TAnimal = object
    procedure Speak; virtual;
  end;

procedure TAnimal.Speak; virtual;
begin
end;

begin
end.

(*
CHECK: expected 'begin', got 'virtual'
*)
