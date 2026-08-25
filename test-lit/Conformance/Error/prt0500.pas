(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 500: Missing type identifer after '^'

}

program iso7185prt0500;

var a: ^;

begin

   a := nil

end.
