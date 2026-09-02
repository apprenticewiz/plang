(*
Issue #730 companion: the fix for checkCallExpr/checkCallStmt (SemaExpr.cpp/
SemaStmt.cpp) only changes what happens when CALL syntax ('X;' / 'X();' /
'X(args)') is used on a name that resolves to a self-scope FIELD symbol but
a same-named sibling METHOD also exists -- it must not change plain (non-
call) reads or writes of that same field, which go through checkIdent, not
checkCallExpr/checkCallStmt, and were never broken by the original bug.
This exercises a read (as a writeln argument, an ordinary value context)
and a write (as an assignment target) of the field X, alongside an
unqualified CALL to the sibling method X, all inside one method body of the
type that declares both -- confirmed against a local `fpc -Mtp` build of
this identical program, which compiles clean and prints exactly the CHECK
lines below.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program FieldStillPlainFieldWithoutCallSyntax;

type
  TBase = object
    X: Integer;
    constructor Init;
  end;
  TChild = object(TBase)
    constructor Init;
    procedure X;
    procedure Run;
  end;

constructor TBase.Init; begin X := 42; end;
constructor TChild.Init; begin inherited Init; end;
procedure TChild.X; begin writeln('TChild.X method called'); end;

procedure TChild.Run;
begin
  writeln('X field before: ', X);
  X := X + 1;
  writeln('X field after: ', X);
  X;
end;

var C: TChild;
begin
  C.Init;
  C.Run;
end.

(*
CHECK:X field before: 42
CHECK-NEXT:X field after: 43
CHECK-NEXT:TChild.X method called
*)
