(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1000: Missing leading '(' for subexpression

}

program iso7185prt1000(output);

var a, b: integer;

begin

   a := b)  

end.
