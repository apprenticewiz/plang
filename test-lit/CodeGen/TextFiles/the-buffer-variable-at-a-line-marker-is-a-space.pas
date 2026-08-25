(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:'ab '
*)

program p(output);
var f: text; c: char;
begin
  rewrite(f); writeln(f, 'ab');
  reset(f); write('''');
  while not eof(f) do begin c := f^; get(f); write(c) end;
  writeln('''')
end.
