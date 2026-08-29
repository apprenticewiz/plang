(*
read()'s dispatch used to pick WHICH runtime reader to call from the LLVM
type alone (readFnSuffix(ty)): any i8 destination was taken for a Char and
routed to plang_read_char, a defensible shortcut before Turbo's sized-integer
ladder existed (ISO 7185/EP stamp Width=64 on every Integer, so the only i8
ordinal that could ever reach it really was a Char).  ShortInt and Byte are
the first Integer-kind types that also lower to i8, and the shortcut sent
them through plang_read_char too: `read(b)` on an input of "200" read
exactly one raw character ('2') and stored ITS ASCII CODE (50) as the value,
rather than parsing the whole token as a number -- and left "00" sitting
unread in the stream.  This is the read-side counterpart of the write-side
writesAsChar bug the sized-integer ladder's own landing already fixed;
read's dispatch (readFnSuffix) needed the identical Sema-Kind consultation.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:b=200
CHECK-NEXT:s=-100
CHECK-NEXT:trailer=42
*)

//--- test.pas
var
  b: Byte;
  s: ShortInt;
  trailer: Integer;
begin
  readln(b);
  writeln('b=', b);
  readln(s);
  writeln('s=', s);
  // If the earlier reads had each consumed only one character and left the
  // rest of their own line sitting in the stream, this would desync and
  // read leftover digits instead of the trailer's own line.
  readln(trailer);
  writeln('trailer=', trailer);
end.

//--- stdin.txt
200
-100
42
