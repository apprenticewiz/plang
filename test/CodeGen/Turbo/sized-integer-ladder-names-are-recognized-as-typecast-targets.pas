(*
The Turbo sized-integer ladder (ShortInt/Byte/SmallInt/Word/Cardinal/
LongInt/LongWord/Int64/QWord/AnsiChar/Pointer, Sema::registerBuiltins) is
registered directly in Sema, never through a `type` section the parser
itself observes -- unlike a user-declared type name, which populates
Parser::TypeNames_ via parseTypeDef.  Without pre-seeding TypeNames_ with
these eleven names for -std=turbo, `Byte(SomeWord)` -- an ordinary, common
Turbo typecast idiom -- would silently fail to route through the
TypeCastExpr parse path at all: it would fall through to an ordinary
CallExpr, and Sema would then reject "Byte" as "not callable" instead of
accepting the cast, exactly the same shape of failure a user-defined type
name had before this whole feature existed.  Confirms both a value cast
(Byte(w), truncating a Word's value to its low 8 bits) and the round trip
back (Word(b)) work for a ladder name exactly like they already do for a
user-defined type or a primitive keyword type.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:44
CHECK-NEXT:44
*)

program laddercast;
var
  w: Word;
  b: Byte;
begin
  w := 300;
  b := Byte(w);
  writeln(b);
  writeln(Word(b));
end.
