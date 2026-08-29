(*
Turbo string[N] semantics item, concrete work 3: s[0] is a DELIBERATE
aliasing exception onto the string's own one-byte length prefix, not a bug
-- Ord(s[0]) reads the current length as an ordinary Char, and `s[0] :=
Chr(n)` truncates (or, within the declared capacity, "extends" -- the bytes
beyond the old length are whatever the buffer already held) the string in
place by overwriting its length field directly, the canonical Turbo idiom
for building a string via indexed assignment.  CGIndexAccess.cpp's own
ShortString s[i] arm is a SEPARATE branch from EP's s[i] (which starts at 1
and never admits index 0 at all), with its own 0..declared-capacity bounds.

Also confirms plain s[i] (i >= 1) still reads/writes ordinary characters,
so index 0 is an addition alongside the ordinary indexing, not a
replacement for it.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var s: string[10];
begin
  s := 'hello';
  writeln(ord(s[0]));
  writeln(s[1], s[5]);
  s[1] := 'H';
  writeln(s);
  { Truncate in place by overwriting the length byte directly. }
  s[0] := chr(3);
  writeln(s);
  writeln(ord(s[0]));
end.

(*
CHECK:5
CHECK-NEXT:ho
CHECK-NEXT:Hello
CHECK-NEXT:Hel
CHECK-NEXT:3
*)
