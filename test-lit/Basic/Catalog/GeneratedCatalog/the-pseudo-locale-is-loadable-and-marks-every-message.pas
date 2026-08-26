(*
What the CI install check greps for.  If this passes and the install check
fails, the catalogs were built but installed out of reach. qps_ploc wraps
every message in [! !] by construction, so a single spot-check on
err_no_input_files stands for "the pseudo-locale loaded and is being
applied" without needing to trigger all 200+ messages individually.

RUN: not %plang -fdiagnostics-language=qps_ploc 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: [!error!]: [!no input files!]
*)
