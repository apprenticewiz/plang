(*
Sema::checkTypeCast accepts TypeName(expr) under two independent rules:
both types are ordinal-or-real (a VALUE conversion), or the two types are
exactly the same size (a VARIABLE reinterpretation, Sema::byteSizeOf).
TBigRec here is a 3-byte record and Word is 2 bytes -- neither rule holds
(a record is not ordinal or real, and the sizes disagree), so the cast has
no defined meaning either way and must be refused with a clear diagnostic
rather than silently accepted or crashing codegen.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot cast 'Word' to 'TBigRec': the types must either both be ordinal or real (for a value conversion), or be the same size (for a variable reinterpretation)
*)

program p;
type
  TBigRec = record
    A, B, C: Byte;
  end;
var
  W: Word;
  R: TBigRec;
begin
  R := TBigRec(W);
end.
