(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 802: Missing ',' in parameter list

}

program iso7185prt0802;

procedure test(a, b: integer);

begin

   a := 1;
   b := 1

end;

begin

   test(1 2)

end.
