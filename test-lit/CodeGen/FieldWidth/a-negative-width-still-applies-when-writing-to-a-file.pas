(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi]
*)

program p(output);
var f: text; s: string(10); c: char;
begin
  s := 'hi';
  rewrite(f); write(f, '[', s:-1, ']'); writeln(f);
  reset(f);
  while not eof(f) do begin
    if eoln(f) then begin readln(f); writeln end
    else begin read(f, c); write(c) end
  end
end.
