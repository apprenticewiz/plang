(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:'too much<eoln> too soon<eoln> '
*)

program p(output);
var f: text; c: char;
begin
  rewrite(f); writeln(f, 'too much'); write(f, 'too soon');
  reset(f); write('''');
  while not eof(f) do begin
    if eoln(f) then write('<eoln>');
    read(f, c); write(c)
  end;
  writeln('''')
end.
