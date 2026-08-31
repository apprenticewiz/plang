(*
Turbo Tier 4, Cluster C item 7's shipped `Strings` unit: StrComp/StrLComp/
StrIComp, all confirmed as real Borland/FPC field practice to return a
strcmp(3)-style signed comparison (negative/zero/positive for less/equal/
greater), not just a -1/0/1 tri-state -- and, for StrIComp, case-insensitive.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrComp(equal)=0
CHECK-NEXT:StrComp(Hello,Apple)>0=TRUE
CHECK-NEXT:StrComp(Apple,Hello)<0=TRUE
CHECK-NEXT:StrLComp(1char)=0
CHECK-NEXT:StrIComp(HELLO,hello)=0
*)

program StrCompFamily;
uses Strings;
var
  A, B: array[0..20] of Char;
  I: Integer;
begin
  A[0]:='H'; A[1]:='e'; A[2]:='l'; A[3]:='l'; A[4]:='o'; A[5]:=#0;
  I := StrComp(A, A);
  Writeln('StrComp(equal)=', I);

  B[0]:='A'; B[1]:='p'; B[2]:='p'; B[3]:='l'; B[4]:='e'; B[5]:=#0;
  I := StrComp(A, B);
  Writeln('StrComp(Hello,Apple)>0=', I > 0);
  I := StrComp(B, A);
  Writeln('StrComp(Apple,Hello)<0=', I < 0);

  // First character agrees ('H' both), StrLComp bounded to 1 char sees no
  // difference even though the full strings differ.
  B[0]:='H'; B[1]:='x'; B[2]:=#0;
  I := StrLComp(A, B, 1);
  Writeln('StrLComp(1char)=', I);

  A[0]:='H'; A[1]:='E'; A[2]:='L'; A[3]:='L'; A[4]:='O'; A[5]:=#0;
  B[0]:='h'; B[1]:='e'; B[2]:='l'; B[3]:='l'; B[4]:='o'; B[5]:=#0;
  I := StrIComp(A, B);
  Writeln('StrIComp(HELLO,hello)=', I);
end.
