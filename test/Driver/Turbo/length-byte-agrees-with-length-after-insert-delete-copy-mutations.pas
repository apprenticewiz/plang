(*
Tier 2 capstone: 'Ord(s[0]) = Length(s)' after a whole SEQUENCE of Insert/
Delete/Copy/SetLength mutations, not just at fresh assignment -- proving
the length BYTE (s[0], read directly through ordinary indexing) and every
string routine that mutates s all agree on the same underlying field after
real, repeated mutation, not merely that each one independently gets its
own first write right.  Every plang_sstr_* mutator (Insert/Delete/
SetLength) writes s[0] as its very last step (plang_sstr.cpp), so this is
really checking that no intermediate step of a longer sequence -- an
Insert immediately followed by a Delete, a Copy of a string mid-mutation,
Length() read between two other mutations -- ever leaves a stale length
byte behind.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Hello TRUE
CHECK-NEXT:HelXXlo TRUE
CHECK-NEXT:Hel TRUE
CHECK-NEXT:Hel!! TRUE
CHECK-NEXT:Hel! TRUE
CHECK-NEXT:He TRUE
*)

program length_byte_agrees_after_mutation;
var
  s, copyOfS: string[20];
begin
  s := 'Hello';
  writeln(s, ' ', Ord(s[0]) = Length(s));

  Insert('XX', s, 4);
  writeln(s, ' ', Ord(s[0]) = Length(s));

  Delete(s, 4, 4);
  writeln(s, ' ', Ord(s[0]) = Length(s));

  Insert('!!', s, Length(s) + 1);
  writeln(s, ' ', Ord(s[0]) = Length(s));

  SetLength(s, Length(s) - 1);
  writeln(s, ' ', Ord(s[0]) = Length(s));

  copyOfS := Copy(s, 1, 2);
  s := copyOfS;
  writeln(s, ' ', Ord(s[0]) = Length(s));
end.
