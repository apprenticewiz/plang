(*
Issue #781 companion, and a correction of a previously-wrong assumption in
Turbo/Objects/a-field-sharing-a-name-with-a-sibling-method-is-still-plain-
field-access-without-call-syntax.pas (deleted by the same change that added
this test): that test claimed a same-named sibling PROCEDURE method left a
bare read/write of the inherited FIELD unaffected, "confirmed against a
local `fpc -Mtp` build" -- but re-verifying against a real `fpc -Mtp` build
of that identical program shows it does NOT compile clean; real fpc reports
exactly the errors below, because a same-named METHOD entirely shadows the
field for every unqualified spelling (#781's own fix, checkIdent/checkField),
and a PROCEDURE method (unlike a parameterless FUNCTION method) has no
result to read a value FROM at all -- err_proc_as_value, the same diagnostic
a plain top-level procedure's bare name already gets (see
bare-implicit-method-reads-refuse-a-procedure-and-a-parameterized-function.
pas, this same directory, for the parameterless-function-taking-arguments
sibling case). Exercised both from INSIDE a sibling method (the unqualified
spelling) and from OUTSIDE any method (the qualified one); the bare CALL
STATEMENT spelling ('X;') is unaffected by this and still calls the method
cleanly (#730, unqualified-call... .pas in Turbo/Objects) -- only READING or
WRITING the name as a value is refused.

RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program FieldShadowedByProcMethodCannotBeReadOrWrittenBare;

type
  TBase = object
    X: Integer;
    constructor Init;
  end;
  TChild = object(TBase)
    constructor Init;
    procedure X;
    procedure ReadInside;
  end;

constructor TBase.Init; begin X := 42; end;
constructor TChild.Init; begin inherited Init; end;
procedure TChild.X; begin writeln('TChild.X method called'); end;

procedure TChild.ReadInside;
var v: Integer;
begin
  v := X;
end;

var
  C: TChild;
  w: Integer;
begin
  C.Init;
  w := C.X;
end.

(*
CHECK: error: procedure 'X' cannot be used as a value
CHECK: error: procedure 'X' cannot be used as a value
*)
