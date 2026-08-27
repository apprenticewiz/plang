(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* issue #234: plang_extend reopens (or reuses) F's stream to append to it,
   with no regard for whatever partial line an earlier write left
   unterminated -- so appending after it used to glue straight onto that
   line instead of starting a new one on disk: 'appended' read back fused
   onto 'partial-line' as a single line, with only one <eoln> below instead
   of two. Fixed, plang_extend finishes the outstanding line first, the same
   as plang_close/plang_reset/plang_rewrite already do -- and there is no
   close between the write and the extend below, so this exercises exactly
   extend's own fix and not close's. *)

(*
CHECK:'partial-line<eoln> appended<eoln> '
*)

program p;
var f: text; c: char;
begin
  rewrite(f, 'issue234-extend-glue.txt'); write(f, 'partial-line');
  extend(f, 'issue234-extend-glue.txt'); writeln(f, 'appended');
  reset(f, 'issue234-extend-glue.txt'); write('''');
  while not eof(f) do begin
    if eoln(f) then write('<eoln>');
    read(f, c); write(c)
  end;
  writeln('''')
end.
