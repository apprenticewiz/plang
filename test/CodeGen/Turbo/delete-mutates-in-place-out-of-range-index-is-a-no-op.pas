(*
System-unit string routines item: Delete(var s, index, count) removes count
characters starting at index, MUTATING s in place.  Unlike Copy, an
out-of-range index (< 1 or > Length(s)) makes the whole call a NO-OP rather
than clamping -- confirmed against a local `fpc -Mtp` install: Delete(s, 0,
2) and Delete(s, 100, 3) both leave s completely unchanged.  count is still
clamped to what is actually available (and to >= 0) -- see
plang_sstr_delete's own doc comment (plang_sstr.cpp).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
begin
  s := 'Hello, World!';
  Delete(s, 6, 7);
  writeln(s);

  s := 'Hello';
  Delete(s, 100, 3);
  writeln(s);

  s := 'Hello';
  Delete(s, 3, 100);
  writeln(s);

  s := 'Hello';
  Delete(s, 0, 2);
  writeln(s);
end.

(*
CHECK:Hello!
CHECK-NEXT:Hello
CHECK-NEXT:He
CHECK-NEXT:Hello
*)
