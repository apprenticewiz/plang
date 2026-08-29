(*
The four write-formatting reversals (booleans, a field width as a minimum
that never truncates, a zero-width char write, and the real-exponent
letter's case) each have their own dedicated Turbo-only test, and each of
those already cites its own ISO 7185/EP sibling baseline file by name as the
non-regression check it must not have moved.  What none of them does is
compile the very SAME source file under both dialects side by side in ONE
lit file, the shape that most directly proves the reversal really is a
function of -std alone and not of two independently-written programs that
might have quietly drifted apart from each other over time.  This file is
that: one source exercising all four points at once, two RUN lines
differing only in -std, both outputs checked here.

Confirmed against a real build before being written down here (this is not
guessed): under -std=turbo the program prints TRUE/FALSE (uppercase),
'hello':2 and 'hello':0 both print the whole word unshortened, 'x':0 still
prints x, and the real exponent uses an uppercase E; under -std=iso7185 it
prints true/false (lowercase), 'hello':2 truncates to two characters and
'hello':0 prints nothing, 'x':0 prints nothing, and the real exponent uses a
lowercase e -- otherwise byte-identical field width and digit counts in
both.
*)

(*
RUN: %plang -std=turbo %s -o %t.turbo
RUN: %run %t.turbo | FileCheck --check-prefix=TURBO --strict-whitespace --match-full-lines %s

RUN: %plang -std=iso7185 %s -o %t.iso
RUN: %run %t.iso | FileCheck --check-prefix=ISO --strict-whitespace --match-full-lines %s
*)

(*
TURBO:TRUE
TURBO-NEXT:FALSE
TURBO-NEXT:[hello]
TURBO-NEXT:[hello]
TURBO-NEXT:[x]
TURBO-NEXT: 2.0000000000000000E+000
TURBO-NEXT: 1.0000000000000000E-010

ISO:true
ISO-NEXT:false
ISO-NEXT:[he]
ISO-NEXT:[]
ISO-NEXT:[]
ISO-NEXT: 2.0000000000000000e+000
ISO-NEXT: 1.0000000000000000e-010
*)

program p;
begin
  writeln(true);
  writeln(false);
  write('['); write('hello':2); writeln(']');
  write('['); write('hello':0); writeln(']');
  write('['); write('x':0);     writeln(']');
  writeln(2.0);
  writeln(1.0e-10)
end.
