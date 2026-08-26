(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1102: Missing first operand to '/'

}

program iso7185prt1102(output);

var a: real; 
    b: integer;

begin

   a := /b  

end.
