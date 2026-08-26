(*
RUN: not %plang -dump-ast %s
*)

{

PRT test 515: Missing type on file type

}

program iso7185prt0515;

var a: file of;

begin

   rewrite(a)

end.
