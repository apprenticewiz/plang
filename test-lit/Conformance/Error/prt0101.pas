(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 101: Missing label ":"

}

program iso7185prt0101;

label 1;

begin

   goto 1;

   1 if 1=1 then

end.
