(*
Truncate(f) discards everything from the CURRENT position to the file's
previous end, leaving everything before it untouched.  Checked two ways:
the reopened file's own FileSize (in records), and the raw on-disk byte
count via `wc -c` (portable across GNU/BSD `wc`, unlike `od`'s column
padding -- see file-of-char-is-a-genuine-binary-file-not-text.pas's own
comment on that pitfall, deliberately avoided here too).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: wc -c < truncate-shortens-a-file-at-the-current-position.bin | tr -d ' ' | FileCheck --check-prefix=SIZE %s
*)

(*
CHECK:FileSize after truncate=3
SIZE:3
*)

var
  f: file of Byte;
  i: Byte;
begin
  assign(f, 'truncate-shortens-a-file-at-the-current-position.bin');
  rewrite(f);
  for i := 1 to 10 do write(f, i);
  seek(f, 3);
  truncate(f);
  close(f);

  reset(f);
  writeln('FileSize after truncate=', filesize(f));
  close(f);
end.
