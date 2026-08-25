(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:'how now<eoln> brown cow<eoln> '
*)

program p(output);
var f: text; c: char;
begin
  rewrite(f); writeln(f, 'how now'); writeln(f, 'brown cow');
  reset(f); write('''');
  while not eof(f) do begin
    if eoln(f) then write('<eoln>');
    read(f, c); write(c)
  end;
  writeln('''')
end.
