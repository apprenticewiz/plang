(*
Turbo Tier 4, Cluster C item 7's shipped `Strings` unit: StrNew/StrDispose.
Real Borland/FPC field practice: StrNew heap-allocates a fresh, independent
copy (this project's own Tier 3 GetMem, runtime/plang_sys.cpp's
plang_tp_getmem, reused directly rather than std::malloc -- see
runtime/plang_strings.cpp's own header comment), StrDispose frees it
(plang_tp_freemem), and StrNew(nil) is nil rather than an allocation of a
zero-length copy.  Mutating the copy independently of the original proves it
is really a separate allocation, not just the same pointer handed back.

RUN: env PLANG_UNIT_DIR=%plang_units_dir %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:StrNew=Hello
CHECK-NEXT:StrNew-is-a-copy=TRUE
CHECK-NEXT:Original-unchanged=Hello
CHECK-NEXT:StrNew(nil)=NIL
*)

program StrNewDispose;
uses Strings;
var
  Src: array[0..20] of Char;
  PSrc, Copy: PChar;
begin
  Src[0]:='H'; Src[1]:='e'; Src[2]:='l'; Src[3]:='l'; Src[4]:='o'; Src[5]:=#0;
  PSrc := Src;

  Copy := StrNew(Src);
  Writeln('StrNew=', Copy);
  Writeln('StrNew-is-a-copy=', Copy <> PSrc);

  Copy[0] := 'J'; // mutate the copy only
  Writeln('Original-unchanged=', PSrc);
  StrDispose(Copy);

  Copy := StrNew(nil);
  if Copy = nil then
    Writeln('StrNew(nil)=NIL')
  else
    Writeln('StrNew(nil)=NOT-NIL');
  StrDispose(nil); // must be a safe no-op
end.
