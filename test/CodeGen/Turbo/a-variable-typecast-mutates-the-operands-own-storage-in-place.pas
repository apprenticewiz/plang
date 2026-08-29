(*
TByteRec(SomeWord).Lo := 0 is a VARIABLE typecast -- an lvalue that
reinterprets SomeWord's own two bytes of storage in place, not a copy of
them.  This is the whole point of TypeCastExpr being its own NodeKind
rather than a CallExpr (see TypeCastExpr's own comment, AstExpr.h): a write
through the cast has to land in W's own storage, so a read of W directly
afterward -- through no cast at all -- must see the change.  Also confirms
the read side: TByteRec(W) used as a plain value (not assigned to) reads
the same reinterpreted bytes back out.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before 258
CHECK-NEXT:after 261
CHECK-NEXT:lo 5 hi 1
*)

program p;
type
  TByteRec = record
    Lo, Hi: Byte;
  end;
var
  W: Word;
  BR: TByteRec;
begin
  W := 258; { little-endian: Lo=2, Hi=1 }
  writeln('before ', W);

  TByteRec(W).Lo := 5; { write through the cast: mutates W itself }
  writeln('after ', W);

  BR := TByteRec(W); { read through the cast: a genuine copy this time }
  writeln('lo ', BR.Lo, ' hi ', BR.Hi);
end.
