(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 517: Missing 'of' on set type

}

program iso7185prt0517;

var a: set char;

begin

   a := []

end.
