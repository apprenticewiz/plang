(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 907: Field and fraction with missing field

}

program iso7185prt0907(output);

begin

    
   write(1.0: :3)

end.
