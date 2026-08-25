(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1100: Missing first operand to '*'

}

program iso7185prt1100(output);

var a, b: integer;

begin

   a := *b  

end.
