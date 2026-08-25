(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 1622: Nested comments

}

program iso7185prt1622;

begin

   { hi there (* george *) again... }

end.
