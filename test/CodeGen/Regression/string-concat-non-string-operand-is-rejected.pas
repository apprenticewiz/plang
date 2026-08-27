(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: operator '+' requires numeric operands, got 'string(5)' and 'Rec'
*)

program p(output);
type
  Rec = record
    x: integer;
  end;
var
  s: string;
  r: Rec;
begin
  s := 'hello' + r
end.
