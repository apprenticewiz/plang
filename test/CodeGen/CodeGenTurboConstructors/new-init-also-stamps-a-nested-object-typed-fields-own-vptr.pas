(*
Issue #511's own fix (stampFieldVptrs, CodeGenProcs.cpp) is called from
TWO places, mirroring stampVptr's own pair of call sites: emitVarValueInit
(a directly declared local/global -- the sibling tests in
CodeGenTurboVirtualDispatch) and, here, New(P, Ctor(...))'s own freshly
allocated memory, right beside the StampVptr call that already stamps the
allocated object's OWN top-level '_vptr' (see new-init-allocates-stamps-
the-vptr-and-runs-the-constructor.pas, this directory, for that baseline).

TCar has no virtual method of its own reachable without going through
Engine, so this specifically proves the NESTED field's vptr -- not just
that New/Init's own pre-existing stamping still works.  Before StampVptr's
closure gained this second call, New(P, Init) allocated and zero-filled
TCar's memory (plang_new uses calloc), stamped only TCar's own slot (which
TCar does not even have, having no virtual method itself), and left
Engine's '_vptr' null; Engine.Rev, a virtual call, would dereference that
null vptr.  Confirmed against a local `fpc -Mtp` build: identical single
'VROOOOM' line (TTurboEngine's own override), exit 0.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program newnested;
type
  TEngine = object
    constructor Init;
    procedure Rev; virtual;
  end;
  TTurboEngine = object(TEngine)
    procedure Rev; virtual;
  end;
  TCar = object
    Engine: TTurboEngine;
    constructor Init;
  end;
constructor TEngine.Init; begin end;
procedure TEngine.Rev; begin WriteLn('vroom'); end;
procedure TTurboEngine.Rev; begin WriteLn('VROOOOM'); end;
constructor TCar.Init; begin Engine.Init; end;
var
  P: ^TCar;
begin
  New(P, Init);
  P^.Engine.Rev;
end.

(*
CHECK:VROOOOM
*)
