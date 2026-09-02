(*
Issue #795: past Int64::max, Parser::parseFactor now retries the literal's
text as a uint64_t before giving up (ParseExpr.cpp), so a value that fits
UInt64 (like the one this test used to use, 10000000000000000000) is no
longer a PARSE-time error at all -- it parses successfully and is only
judged, in full Sema, against wherever it's actually used (see
test/CodeGen/Turbo/qword-literal-past-int64-max-rejected-for-signed-
destination-issue-795.pas for that Sema-level coverage). This test is about
the PARSER's own handling of a value with no representation either way, so
it now uses one past even UInt64::max (18446744073709551615) to keep
exercising that -- still rejected here, at parse time, before Sema ever
runs.

RUN: not %plang_ir -dump-parse-tree %s
*)

program p; var x : integer; begin x := 100000000000000000000 end.
