(*
Issue #730: real Turbo Pascal object types allow a descendant's own METHOD
to reuse an ancestor FIELD's name (only a new FIELD reusing an inherited
name is barred -- see SemaType.cpp's resolveObjectType comment, and
descendant-method-reusing-an-inherited-field-name-is-legal.pas, which
already confirms the declaration alone compiles clean).  But an unqualified
CALL to such a method, from inside a method body of the same object type,
used to resolve to the inherited FIELD instead of the sibling METHOD:
pushMethodSelfScope (Sema.cpp) registers every field (own and inherited) as
an ordinary Var symbol, and checkCallStmt's plain Symtab.lookup found that
Var symbol first, reporting a spurious "'X' is not callable" on legal code
-- confirmed against a local `fpc -Mtp` build of this identical program,
which compiles clean and prints exactly the CHECK line below.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program FieldMethodNameClash;

type
  TBase = object
    X: Integer;
    constructor Init;
  end;
  TChild = object(TBase)
    constructor Init;
    procedure X;
    procedure CallIt;
  end;

constructor TBase.Init; begin X := 42; end;
constructor TChild.Init; begin inherited Init; end;
procedure TChild.X; begin writeln('TChild.X method called'); end;
procedure TChild.CallIt;
begin
  X;
end;

var C: TChild;
begin
  C.Init;
  C.CallIt;
end.

(*
CHECK:TChild.X method called
*)
