(*
System-unit string routines item: SetLength(var s, newLength) sets s's own
length byte directly, clamped to [0, s's declared capacity].  Bytes exposed
by growing are left exactly as they were (no zero-fill) -- confirmed against
a local `fpc -Mtp` install that real Turbo/FPC does not touch them either,
which is why this test only checks the reported LENGTH after growing, not
the content of the newly-exposed bytes (whatever they happen to hold is not
part of this routine's contract).  Two DELIBERATE divergences from real fpc
-Mtp, both for memory/input-validation safety -- see plang_sstr_setlength's
own doc comment (plang_sstr.cpp) for the full transcript: growing past a
narrow string[N]'s own declared capacity is CLAMPED here (real fpc lets it
write past the variable's own physical storage, `Runtime error 201` under
`-Cr`), and a negative newLength is clamped to 0 (real fpc reinterprets it as
a raw byte, so -1 becomes 255).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s5: string[5];
begin
  s5 := 'abc';
  SetLength(s5, 2);
  writeln(s5, ' ', Length(s5));

  s5 := 'ab';
  SetLength(s5, 5);
  writeln(Length(s5));

  s5 := 'ab';
  SetLength(s5, 100);
  writeln(Length(s5));

  s5 := 'ab';
  SetLength(s5, -1);
  writeln(Length(s5));
end.

(*
CHECK:ab 2
CHECK-NEXT:5
CHECK-NEXT:5
CHECK-NEXT:0
*)
