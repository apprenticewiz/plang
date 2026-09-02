(*
Issue #627: 'MakeBox()^.Put(7);' -- a statement-position method call whose
RECEIVER is a further dereference of a function call's own result -- used to
be rejected by the parser ("expected 'end', got '^'"): the statement parser
special-cased a bare leading identifier immediately followed by '(' as the
WHOLE statement (an ordinary CallStmt), consuming 'MakeBox()' and returning
before ever looking for a '.', '^', or '[' still pending afterward, unlike
expression-position parsing (`x := MakeBox()^.Get();`, already legal),
which always wraps a call in parsePostfix before returning it.  Both the
bare method-call form ('...^.Put;', no parens -- verified separately by
a-caret-dereferenced-pointer-receiver-....pas for a plain variable receiver,
not a call's own result) and an assignment through the same chain are
exercised here too, confirmed against a local `fpc -Mtp` build to accept
all three shapes.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TBox = object
    Value: Integer;
    procedure Put(V: Integer);
    procedure Announce;
  end;

procedure TBox.Put(V: Integer);
begin
  Value := V;
end;

procedure TBox.Announce;
begin
  writeln('Value=', Value);
end;

var
  Shared: ^TBox;

function MakeBox: ^TBox;
begin
  MakeBox := Shared;
end;

begin
  New(Shared);
  MakeBox()^.Put(7);
  MakeBox()^.Announce;
  MakeBox()^.Value := 55;
  MakeBox()^.Announce();
  Dispose(Shared);
end.

(*
CHECK:Value=7
CHECK-NEXT:Value=55
*)
