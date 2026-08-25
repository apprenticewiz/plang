(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 202: Missing second field ident

}

program iso7185prt0202;

var a: record b,: integer end;

begin

   a.b := 1

end.
