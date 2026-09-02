(*
Issue #675: a TotalWidth exceeding INT32_MAX used to reach checkedWidth's
unconditional plang_err_field_width abort (exit 70, the ISO/EP dialect's own
error-reporting channel) even under -std=turbo with the I-minus directive
active -- a dialect-reporter leak, not a considered choice: Turbo has its
own resumable InOutRes/I-minus mechanism for exactly this kind of failure, and real
`fpc -Mtp` does not even treat an out-of-range width as a failure at all.
Confirmed against `fpc -Mtp` 3.2.2: it narrows the int64 width the same way
an implicit int64->int32 conversion would, silently wrapping -- runtime/
plang_file.cpp's wrapWidthTP now does the identical thing ahead of every
Turbo write-with-width entry point (write(f, ...) against an explicit file
variable; a bare write/Str shares plang_io.cpp's own, dialect-agnostic
writers instead and is out of this item's scope), so the value checkedWidth
ever sees is already back in range and the abort this item fixes can no
longer trigger.

Two widths are exercised, matching the two ways an int64->int32 wrap can
land: W := 4294967296 + 5 (2**32 + 5) wraps to a SMALL positive int32 (5) --
deliberately chosen over a width like 5000000000 (which would wrap to
705032704 and pad a test file with that many spaces) so this stays a fast,
small test while still exercising a genuinely-overflowing W; W :=
3000000000 wraps NEGATIVE (-1294967296), which this file's own existing
"negative width means no padding, write the value in full" rule then
already handles correctly -- confirmed against `fpc -Mtp`: `write(f,
'AB':w)` with that W writes "AB" unpadded.  Both leave IOResult at 0 and let
the process exit normally, which is this item's actual fix.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:small-positive-wrap-ioresult=0
CHECK-NEXT:small-positive-wrap-contents=[    X]
CHECK-NEXT:negative-wrap-ioresult=0
CHECK-NEXT:negative-wrap-contents=[AB]
*)

var
  f: text;
  w: int64;
  line: string;
begin
  {$I-}

  { W = 2**32 + 5 wraps to the small positive int32 5: a genuinely
    overflowing width (well past INT32_MAX), but one that pads only 4
    characters rather than hundreds of millions of them. }
  assign(f, 'write-field-width-overflow-wraps-instead-of-aborting-issue-675-a.txt');
  rewrite(f);
  w := 4294967296 + 5;
  write(f, 'X':w);
  writeln('small-positive-wrap-ioresult=', IOResult);
  close(f);
  reset(f);
  readln(f, line);
  writeln('small-positive-wrap-contents=[', line, ']');
  close(f);

  { W = 3000000000 wraps to the negative int32 -1294967296: the existing
    negative-width rule applies, so the value is written in full, unpadded. }
  assign(f, 'write-field-width-overflow-wraps-instead-of-aborting-issue-675-b.txt');
  rewrite(f);
  w := 3000000000;
  write(f, 'AB':w);
  writeln('negative-wrap-ioresult=', IOResult);
  close(f);
  reset(f);
  readln(f, line);
  writeln('negative-wrap-contents=[', line, ']');
  close(f);
end.
