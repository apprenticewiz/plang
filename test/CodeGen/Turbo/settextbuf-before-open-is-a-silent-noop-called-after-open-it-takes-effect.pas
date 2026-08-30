(*
SetTextBuf(f, buf[, size]) overrides f's own internal I/O buffer.  plang's
file model is a thin wrapper over C stdio, not Borland's own hand-rolled
TextRec buffering layer, and has no "pending buffer, not yet attached to a
stream" slot the way TextRec does -- see runtime/plang_file.cpp's
plang_tp_settextbuf for the full, DELIBERATE, DOCUMENTED deviation this
implies: called BEFORE f is opened (real Turbo Pascal's own idiom), there
is no stream yet to attach a buffer to, so it is a silent no-op that does
not crash and does not stop the file from working normally; called AFTER f
is opened, it takes effect immediately via setvbuf(3).  Both cases are
exercised here, and both still produce correct file content either way --
the point of this test is "does not crash and does not corrupt output in
either calling order", not a byte-level proof of which buffer is actually
in use (SetTextBuf's own effect is not independently observable from
plang Pascal code, only from its absence of ill effect).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t
RUN: cat settextbuf-before-open-is-a-silent-noop-called-after-open-it-takes-effect-1.txt | FileCheck --check-prefix=BEFORE %s
RUN: cat settextbuf-before-open-is-a-silent-noop-called-after-open-it-takes-effect-2.txt | FileCheck --check-prefix=AFTER %s
*)

(*
BEFORE:before-open-still-works
AFTER:after-open-buffered
*)

var
  f: text;
  buf: array[0..511] of Char;
begin
  assign(f, 'settextbuf-before-open-is-a-silent-noop-called-after-open-it-takes-effect-1.txt');
  settextbuf(f, buf, 256);
  rewrite(f);
  writeln(f, 'before-open-still-works');
  close(f);

  assign(f, 'settextbuf-before-open-is-a-silent-noop-called-after-open-it-takes-effect-2.txt');
  rewrite(f);
  settextbuf(f, buf, 256);
  writeln(f, 'after-open-buffered');
  close(f);
end.
