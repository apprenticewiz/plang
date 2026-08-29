(*
Tier 2 capstone: PChar arithmetic and p[i] indexing combined with a
ShortString buffer in one program -- a realistic "build a string via
pointer walking" idiom real Turbo code actually uses (reading a
NUL-terminated C buffer character by character into a Pascal string), then
the reverse direction: walking a SECOND PChar directly over the resulting
ShortString's own storage to mutate it in place.

Part 1: 'src' is a zero-based char array holding two NUL-terminated C
strings back to back ('Hello'#0'World'#0).  'p := src' decays the array to
a PChar with no '@' needed; the loop walks p[len] until the NUL terminator,
poking each byte directly into 's' via ShortString's ORDINARY 1-based
indexing (s[len+1] := p[len]) -- and then sets s[0], the length byte,
BY HAND (Chr(len)) rather than through SetLength, to prove indexing at
position 0 really does alias the same length field SetLength/Length read.

Part 2: 'p := p + len + 1' pointer-arithmetics past 'Hello' and its NUL
terminator, landing on 'World' -- appended into 's' the same byte-poking
way, this time growing s[0] incrementally instead of computing the final
length up front, showing the length byte tracks a MUTATING in-progress
length correctly, not just a final one.

Part 3: 'q := @s[1]' takes the address of the ShortString's own FIRST
character -- itself typed Char, so '^Char' qualifies for PChar's
structural (pointee-is-Char) gate the same as any other Char pointer would
-- and walks q[i] across s's own storage, upcasing every byte IN PLACE.
Since q and s share the same memory (q points directly at s's data, not a
copy), 's' itself reads back upcased afterward with no separate
assignment -- the same "genuinely shares storage" property
absolute-overlays-shared-storage.pas proves for 'absolute', now shown for
a PChar taken from inside a ShortString's own bytes.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Hello 5 TRUE
CHECK-NEXT:HelloWorld 10 TRUE
CHECK-NEXT:HELLOWORLD
*)

program pchar_builds_shortstring;
var
  src: array[0..11] of Char;
  p, q: PChar;
  s: string[20];
  i, len: Integer;
begin
  src[0] := 'H'; src[1]  := 'e'; src[2]  := 'l'; src[3]  := 'l';
  src[4] := 'o'; src[5]  := #0;
  src[6] := 'W'; src[7]  := 'o'; src[8]  := 'r'; src[9]  := 'l';
  src[10] := 'd'; src[11] := #0;

  p := src;
  len := 0;
  while p[len] <> #0 do begin
    s[len + 1] := p[len];
    len := len + 1;
  end;
  s[0] := Chr(len);
  writeln(s, ' ', Length(s), ' ', Ord(s[0]) = Length(s));

  p := p + len + 1;
  while p[0] <> #0 do begin
    len := len + 1;
    s[len] := p[0];
    s[0] := Chr(len);
    p := p + 1;
  end;
  writeln(s, ' ', Length(s), ' ', Ord(s[0]) = Length(s));

  q := @s[1];
  for i := 0 to Length(s) - 1 do
    q[i] := UpCase(q[i]);
  writeln(s);
end.
