(*
Non-regression/coverage: ISO §6.4.3.2's string-type (a packed array[1..n] of
char) carries no dialect gate of its own -- it is a plain array type every
dialect, Turbo included, can declare -- so writing/reading one to/from a
text file is reachable from Turbo exactly as from ISO/EP, through
plang_str_write_file/plang_str_write_file_w (write) and
plang_str_read_fixed_file (read), three of the ~23 functions this item
gave a genuinely separate `_turbo` sibling to.  Confirms the ordinary,
no-error round trip still works correctly through those new siblings: five
letters are written padded to a 10-wide array, read back into a FRESH
array of the same type, and printed character by character.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello]
*)

var f: text;
    a, b: packed array[1..10] of char;
begin
  assign(f, 'packed-array-of-char-file-io-still-works-under-turbo.txt');
  rewrite(f);
  a := 'hello     ';
  write(f, a);
  writeln(f);
  close(f);

  assign(f, 'packed-array-of-char-file-io-still-works-under-turbo.txt');
  reset(f);
  readln(f, b);
  writeln('[', b[1], b[2], b[3], b[4], b[5], ']');
  close(f);
end.
