(*
Rename(f, newname) renames the file f is bound to ON DISK, and updates f's
OWN bound name to match -- confirmed against `fpc -Mtp`: a following
Reset(f) with no intervening Assign opens the NEW name, not the old one.
Checked three ways: the old name is gone from disk (`not test -f`), the new
name exists with the right content (`wc -c`), and the SAME plang program
reopens f (via its own updated bound name) and reads back what it wrote
before the rename.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: not test -f rename-renames-the-bound-file-on-disk-and-updates-fs-own-bound-name-old.bin
RUN: wc -c < rename-renames-the-bound-file-on-disk-and-updates-fs-own-bound-name-new.bin | tr -d ' ' | FileCheck --check-prefix=SIZE %s
*)

(*
CHECK:rename IOResult=0
CHECK-NEXT:value after reopening the renamed file=42
SIZE:1
*)

var
  f: file of Byte;
  v: Byte;
begin
  assign(f, 'rename-renames-the-bound-file-on-disk-and-updates-fs-own-bound-name-old.bin');
  rewrite(f);
  write(f, Byte(42));
  close(f);

  rename(f, 'rename-renames-the-bound-file-on-disk-and-updates-fs-own-bound-name-new.bin');
  writeln('rename IOResult=', IOResult);

  reset(f);
  read(f, v);
  writeln('value after reopening the renamed file=', v);
  close(f);
end.
