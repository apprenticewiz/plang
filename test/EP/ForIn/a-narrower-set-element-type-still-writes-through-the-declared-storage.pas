(*
Issue #217: `c`'s declared type ('a'..'z', a Subrange) is stored at 64 bits --
every ordinal but Char and Boolean is -- while `set of char`'s element type
is Char, stored at 8.  Requiring an exact LLVM-type match before writing
through the declared variable sent this (entirely legal) mismatch down a
fallback path that minted an unrelated, same-named alloca and bound it into
the CURRENT scope with no push/pop to ever undo it: the rebinding outlived
the loop, so `c := 'y'` after the loop, and `show` -- which had already
captured the real `c`'s address at its declared 64-bit width -- disagreed
about which storage "c" named.  `show` went on reading 8 bytes through a
pointer that, after the loop, addressed a 1-byte stand-in: an out-of-bounds
stack read, not just a wrong answer (confirmed with AddressSanitizer while
this was root-caused: "stack-buffer-overflow ... READ of size 8").

RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:121
*)

program p(output);
var
  c: 'a'..'z';
  s: set of char;

procedure show;
begin
  writeln(ord(c))
end;

begin
  s := ['b'];
  c := 'x';
  for c in s do ;
  c := 'y';
  show
end.
