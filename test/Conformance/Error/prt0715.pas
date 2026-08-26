(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 715: Alternate '=<'

}

program iso7185prt0715;

var b: boolean;

begin

   b := 1 =< 2

end.
