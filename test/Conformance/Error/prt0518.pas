(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 518: Missing type on set type

}

program iso7185prt0518;

var a: set of;

begin

   a := []

end.
