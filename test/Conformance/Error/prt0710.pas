(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 710: Missing second operand to '<='

}

program iso7185prt0710;

var b: boolean;

begin

   b := 1 <=

end.
