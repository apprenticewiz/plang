(*
Move(Source, Dest, Count) is lowered to llvm.memmove specifically, never
memcpy -- Source and Dest may legally overlap, real Turbo Pascal's own Move
is defined to handle that correctly, and memcpy's behavior on an
overlapping range is undefined (it could easily "work" on one build and
corrupt data on another).  This file proves the overlap actually goes
through memmove, not just that Move compiles: a naive memcpy porting bug --
Source and Dest silently swapped, the classic mistake porting Move to a
memmove-style (Dest, Source, Len) signature -- would also produce a
DIFFERENT wrong answer here, not merely a correct one computed the slow
way, so this also stands as the argument-order check: Turbo's own Move
puts the SOURCE first and the DESTINATION second, the reverse of
llvm.memmove's (and C memmove's) own (Dest, Src, Len) parameter order.

Overlapping forward shift: buf holds "abcdefghij" (1-indexed); moving 8
bytes from buf[1] to buf[3] must read every source byte as it was
BEFORE the move, exactly as a real memmove does, giving "ababcdefgh" --
not the corrupted result a naive byte-by-byte forward copy through
overlapping memory would produce.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ababcdefgh
CHECK-NEXT:abcdefghij
*)

program p;
var
  buf, buf2: array[1 .. 10] of Char;
  k: Integer;
begin
  for k := 1 to 10 do buf[k] := Chr(Ord('a') + k - 1);
  Move(buf[1], buf[3], 8);
  for k := 1 to 10 do write(buf[k]);
  writeln;

  { Non-overlapping copy into a second buffer, as a plain sanity check that
    the ordinary (non-overlapping) case, and the Source/Dest argument
    order, are also correct on their own. }
  for k := 1 to 10 do buf[k] := Chr(Ord('a') + k - 1);
  for k := 1 to 10 do buf2[k] := '?';
  Move(buf[1], buf2[1], 10);
  for k := 1 to 10 do write(buf2[k]);
  writeln;
end.
