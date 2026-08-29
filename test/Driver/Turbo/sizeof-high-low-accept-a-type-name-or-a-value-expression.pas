(*
SizeOf/High/Low are the one argument SHAPE in this whole compiler that is
not a plain value expression: their sole argument may instead be a TYPE
NAME.  Two kinds of type name are admitted, and this file exercises both --

  * one of the five PRIMITIVE type keywords (integer, real, boolean, char,
    string) -- lexer KEYWORDS, not identifiers, so the parser needs a
    dedicated production for exactly this argument position
    (Parser::parseSizeHighLowArg) to admit one at all;
  * an ordinary user-defined type name (a record, an enumeration, ...),
    which parses as an IdentExpr exactly like a variable reference would,
    and is told apart from one in Sema (Sema::resolveTypeArgOrValue) by
    what the name actually resolves to.

-- and, like real FPC (this is not ISO/EP; SizeOf/High/Low do not exist
there), a plain VALUE expression is accepted too: SizeOf(x) answers about
x's own type, and High(arr)/Low(arr) about arr's own index type.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:2
CHECK-NEXT:1
CHECK-NEXT:4
CHECK-NEXT:2
CHECK-NEXT:2
CHECK-NEXT:0
CHECK-NEXT:255
CHECK-NEXT:0
CHECK-NEXT:2
CHECK-NEXT:0
CHECK-NEXT:5
CHECK-NEXT:1
*)

program p;
type
  TColor = (Red, Green, Blue);
  TRec = record
    a: Integer;
    b: Byte;
  end;
var
  w: Word;
  r: TRec;
  arr: array[1..5] of Integer;
begin
  w := 0;
  writeln(SizeOf(Integer));    { primitive keyword type name }
  writeln(SizeOf(Word));       { user-defined (Turbo predefined) type name }
  writeln(SizeOf(Boolean));
  writeln(SizeOf(TRec));       { user-defined record type name }
  writeln(SizeOf(w));          { value expression: SizeOf(w's own type) }
  writeln(Ord(High(TColor)));  { user-defined ordinal type name }
  writeln(Ord(Low(TColor)));
  writeln(High(Byte));         { primitive-ladder type name }
  writeln(Low(Byte));
  writeln(High(TColor));       { High(T) itself is a value of type T }
  writeln(Low(TColor));
  writeln(High(arr));          { value expression: bounds of arr's own index type }
  writeln(Low(arr));
  r.a := 0; r.b := 0; { silence unused-field-style concerns; r is otherwise unread }
end.
