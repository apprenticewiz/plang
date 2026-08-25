(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1201: '-' with missing term

}

program iso7185prt1201(output);

var a: integer;

begin

   a := -  

end.
