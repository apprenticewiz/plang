(*
Tier 2 capstone: SizeOf of a record mixing a narrowed subrange, an enum, a
Char, and a Boolean field, checked against a layout worked out by hand from
Sema::byteSizeOf/byteAlignOf's own rules (natural alignment: each field
starts at a multiple of its own alignment, the whole record rounds up to
the widest field's alignment -- "what plang already emits and what FPC
uses by default," SemaType.cpp's own comment).

Field-by-field, in TMixed's declaration order:
  - code:   TCode = 0..250      -- narrows to 1 byte UNSIGNED (Byte), align 1
                                    (TypeContext::narrowestStorage: Lo=0 fits
                                    unsigned 8-bit).             offset 0, size 1
  - n:      TWide = 300..1000   -- narrows to 2 bytes UNSIGNED (Word) -- Lo=300
                                    is still >= 0, so narrowestStorage tries
                                    unsigned before signed at EVERY width, and
                                    1000 fits unsigned 16-bit -- needs 2-byte
                                    alignment, so 1 byte of padding is inserted
                                    after 'code' first.   offset 2 (1 pad), size 2
  - season: TSeason (5 members) -- narrows to 1 byte (Byte-width enum),
                                    align 1.                     offset 4, size 1
  - ch:     Char                -- always 1 byte, align 1.      offset 5, size 1
  - flag:   Boolean             -- strict Boolean is Width=8 always
                                    (Type::makeBoolean), so 1 byte, align 1.
                                                                  offset 6, size 1

Raw total after the last field is 7 bytes; the record's own alignment is the
widest field's (2, from 'n'), so the total size rounds up to 8 -- one more
byte of TAIL padding after 'flag'.  SizeOf(TMixed) is therefore 8, not the
sum of the five fields' own sizes (6): the record has one byte of INTERNAL
padding (before 'n') and one byte of TRAILING padding (after 'flag'), and
this test pins the arithmetic explicitly (8 - 6 = 2 padding bytes) rather
than only the final total, then round-trips every field through a real
assignment to confirm the fields the hand-computed offsets predict are the
ones actually holding each value.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8
CHECK-NEXT:1
CHECK-NEXT:2
CHECK-NEXT:250 1000 2 C TRUE
*)

program sizeof_record_layout;
type
  TCode   = 0..250;
  TWide   = 300..1000;
  TSeason = (Spring, Summer, Fall, Winter, Monsoon);
  TMixed  = record
    code:   TCode;
    n:      TWide;
    season: TSeason;
    ch:     Char;
    flag:   Boolean;
  end;
var
  m: TMixed;
  fieldTotal: Integer;
begin
  writeln(SizeOf(m));
  writeln(SizeOf(m.code));
  fieldTotal := SizeOf(m.code) + SizeOf(m.n) + SizeOf(m.season)
              + SizeOf(m.ch) + SizeOf(m.flag);
  writeln(SizeOf(m) - fieldTotal);

  m.code   := 250;
  m.n      := 1000;
  m.season := Fall;
  m.ch     := 'C';
  m.flag   := true;
  writeln(m.code, ' ', m.n, ' ', Ord(m.season), ' ', m.ch, ' ', m.flag);
end.
