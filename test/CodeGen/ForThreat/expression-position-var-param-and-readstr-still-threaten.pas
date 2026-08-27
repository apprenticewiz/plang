(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: must not be passed as a 'var' parameter
ERR: must not be read into
*)

program p(output);
var i, y: integer;

function f(var v: integer): integer;
begin f := v; v := v + 1 end;

begin
  for i := 1 to 3 do y := f(i);
  for i := 1 to 3 do readstr('55', i);
  writeln(y)
end.
