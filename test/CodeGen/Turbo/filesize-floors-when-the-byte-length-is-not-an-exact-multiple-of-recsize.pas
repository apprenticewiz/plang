(*
FileSize(f) FLOORS when the file's byte length is not an exact multiple of
f's own RecSize -- confirmed against `fpc -Mtp` before this test was
written (this item's own plan explicitly asked this be verified, not
guessed at): a 5-byte-long file, reopened as an untyped file with an
explicit RecSize of 2 (Reset(f, 2), Cluster A item 4's own second-argument
RecSize form), reports FileSize 2 -- not 3 (rounded up) and not a
fractional value.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:2
*)

var
  f: file of Byte;
  u: file;
  i: Byte;
begin
  assign(f, 'filesize-floors-when-the-byte-length-is-not-an-exact-multiple-of-recsize.bin');
  rewrite(f);
  for i := 1 to 5 do write(f, i);
  close(f);

  assign(u, 'filesize-floors-when-the-byte-length-is-not-an-exact-multiple-of-recsize.bin');
  reset(u, 2);
  writeln(filesize(u));
  close(u);
end.
