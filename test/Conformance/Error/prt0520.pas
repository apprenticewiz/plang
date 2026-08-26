(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 520: Misspelled 'record' on field list

}

program iso7185prt0520;

var a: recard a, b: integer end;

begin

   a.a := 1

end.
