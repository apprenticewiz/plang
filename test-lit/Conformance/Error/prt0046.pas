(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 46: Missing final semicolon

}

program iso7185prt0046;

procedure x; begin end

begin

   x

end.
