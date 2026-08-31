(*
Issue #511: the same gap fixed for a record field applies identically to an
OBJECT type's own field, when that field's type is itself a DIFFERENT
object type (not an ancestor -- an ancestor sub-object shares the SAME
'_vptr' slot the outermost stampVptr call already finds, see
stampFieldVptrs' own comment, CodeGenImpl.h).  TCar has no ancestor of its
own; Engine is a genuinely separate, unrelated object type embedded by
value.  Before this fix, declaring 'var C: TCar' stamped C's own '_vptr'
(TCar.Drive is virtual) but never looked inside C's storage for Engine's
own -- so Engine.Rev, a virtual call, read garbage.  Confirmed against a
local `fpc -Mtp` build: identical two lines ('driving' then 'VROOOOM',
TTurboEngine's own override -- Engine's declared/concrete type), exit 0.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program objinobj;
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
    procedure Drive; virtual;
  end;
constructor TEngine.Init; begin end;
procedure TEngine.Rev; begin WriteLn('vroom'); end;
procedure TTurboEngine.Rev; begin WriteLn('VROOOOM'); end;
constructor TCar.Init; begin Engine.Init; end;
procedure TCar.Drive; begin WriteLn('driving'); Engine.Rev; end;
var
  C: TCar;
begin
  C.Init;
  C.Drive;
end.

(*
CHECK:driving
CHECK-NEXT:VROOOOM
*)
