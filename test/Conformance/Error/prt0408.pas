(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 408: Missing ',' between identifiers on enumeration

}

program iso7185prt0408;

var a: (one two);

begin

   a := one

end.
