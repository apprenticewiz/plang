(*
Erase(f) and Rename(f, name) both require f be fmClosed first -- confirmed
against `fpc -Mtp`: calling either against a still-open f sets InOutRes to
102 ("file not assigned" -- FPC's own field practice reuses that code here
rather than a dedicated one) and performs nothing (the file is still open
and readable afterward, proven here by reading its content back unharmed).
Both builtins share the identical fmClosed check (runtime/plang_file.cpp's
plang_tp_erase/plang_tp_rename), so this one test covers both rather than
duplicating the same behavior twice.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:erase while open IOResult=102
CHECK-NEXT:rename while open IOResult=102
CHECK-NEXT:value=42
*)

var
  f: file of Byte;
  v: Byte;
begin
  assign(f, 'erase-and-rename-on-a-still-open-file-set-inoutres-102.bin');
  rewrite(f);
  write(f, Byte(42));

  {$I-}
  erase(f);
  writeln('erase while open IOResult=', IOResult);

  rename(f, 'erase-and-rename-on-a-still-open-file-set-inoutres-102-renamed.bin');
  writeln('rename while open IOResult=', IOResult);
  {$I+}

  close(f);
  reset(f);
  read(f, v);
  writeln('value=', v);
  close(f);
end.
