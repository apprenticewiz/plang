(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1500: Missing leading digit before '.'

}

program iso7185prt1500(output);

var a: integer;

begin

   a := .5

end.
