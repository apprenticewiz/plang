(*
System-unit string routines item: Copy(s, index, count) CLAMPS an
out-of-range index/count into range rather than raising the way EP's
substr does for an out-of-range request (plang_str_substr, plang_str.cpp) --
see plang_sstr_copy's own doc comment (plang_sstr.cpp) for the exact rule,
empirically derived against a local `fpc -Mtp` install.  index < 1 clamps to
1 WITHOUT re-basing count (Copy(s, 0, 5) and Copy(s, 1, 5) give the same five
characters); count < 0 clamps to 0; an index beyond the source's own length
gives an empty result; a count reaching past the end is clamped to what is
actually available.  Copy's result is always a capacity-255 ShortString
regardless of the source's own declared capacity (Builtins.def's own
comment).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
begin
  s := 'Hello, World!';
  writeln(Copy(s, 1, 5));
  writeln(Copy(s, 8, 5));
  writeln(Copy(s, 8, 100));
  writeln(Copy(s, 100, 5));
  writeln(Copy(s, 0, 5));
  writeln(Copy(s, -3, 5));
  writeln(Copy(s, 3, -5));
  writeln(Copy(s, 3, 0));
end.

(*
CHECK:Hello
CHECK-NEXT:World
CHECK-NEXT:World!
CHECK-EMPTY:
CHECK-NEXT:Hello
CHECK-NEXT:Hello
CHECK-EMPTY:
CHECK-EMPTY:
*)
