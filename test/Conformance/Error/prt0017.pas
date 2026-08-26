(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 17: Unstarted label list

}

program iso7185prt0017;

label ,1;

begin

   goto 1;

   1:

end.
