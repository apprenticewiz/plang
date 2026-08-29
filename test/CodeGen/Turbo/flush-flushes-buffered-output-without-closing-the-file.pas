(*
Flush(f) flushes f's buffered output WITHOUT closing it.  Checked from
INSIDE the same program (so exit-time stdio cleanup can never be confused
for what Flush(f) itself did): a SECOND, independent file variable g is
Assign/Reset onto the exact same path while f is still open for writing
and has not been Close'd, and g reads back the bytes f wrote before f's
own Flush call.  Without a real flush, those bytes would still be sitting
in f's own stdio buffer and invisible to a second, independent stream on
the same path.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:read back while f is still open: [hello without closing]
*)

var
  f, g: text;
  s: string;
begin
  assign(f, 'flush-flushes-buffered-output-without-closing-the-file.txt');
  rewrite(f);
  write(f, 'hello without closing');
  flush(f);

  assign(g, 'flush-flushes-buffered-output-without-closing-the-file.txt');
  reset(g);
  readln(g, s);
  writeln('read back while f is still open: [', s, ']');
  close(g);
  close(f);
end.
