(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 519: Missing 'record' on field list

}

program iso7185prt0519;

var a: a, b: integer end;

begin

   a.a := 1

end.
