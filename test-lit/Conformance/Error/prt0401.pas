(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 401: Missing identifier on enumeration

}

program iso7185prt0401;

var a: ();

begin

   a := one

end.
