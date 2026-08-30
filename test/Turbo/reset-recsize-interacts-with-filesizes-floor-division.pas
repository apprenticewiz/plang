(*
Tier 3 capstone (integration): the specific interaction between two
separately-shipped pieces of this tier (Reset(f, N)'s RecSize argument,
Cluster A item 4; FileSize's floor-division for a non-exact-multiple byte
length, Cluster C item 6/PR #485) -- driven by one real 16-byte file
opened TWO different ways.

A file that is genuinely 16 bytes long on disk:
  - reopened untyped with an EXPLICIT RecSize of 4 (Reset(f, 4)) reports
    FileSize 4 (16 div 4, an exact multiple -- the easy case)
  - reopened untyped with NO explicit RecSize at all -- Turbo's own
    untyped-file default of 128 (confirmed against `fpc -Mtp`;
    runtime/plang_file.cpp's own Reset/Rewrite default) -- reports
    FileSize 0 (16 div 128, floored, NOT rounded up to 1), since the file
    is shorter than even one 128-byte record

Both against the exact same on-disk bytes, written once and reopened
twice, rather than two independently-sized fixture files that could
quietly drift apart.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:FileSize with explicit RecSize 4=4
CHECK-NEXT:FileSize with the untyped-file default RecSize (128)=0
*)

var
  typed: file of Byte;
  explicitRecSize, defaultRecSize: file;
  i: Byte;
begin
  assign(typed, 'reset-recsize-interacts-with-filesizes-floor-division.bin');
  rewrite(typed);
  for i := 1 to 16 do write(typed, i); { exactly 16 bytes on disk }
  close(typed);

  assign(explicitRecSize, 'reset-recsize-interacts-with-filesizes-floor-division.bin');
  reset(explicitRecSize, 4);
  writeln('FileSize with explicit RecSize 4=', filesize(explicitRecSize));
  close(explicitRecSize);

  assign(defaultRecSize, 'reset-recsize-interacts-with-filesizes-floor-division.bin');
  reset(defaultRecSize); { no second argument: the untyped-file default RecSize }
  writeln('FileSize with the untyped-file default RecSize (128)=', filesize(defaultRecSize));
  close(defaultRecSize);
end.
