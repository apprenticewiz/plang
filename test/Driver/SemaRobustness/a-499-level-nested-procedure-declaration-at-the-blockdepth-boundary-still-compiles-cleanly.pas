(*
Issue #597: Sema::checkBlock/checkProcBody (Sema.cpp) had NO recursion
guard at all -- not even the "insufficient but present" kind Parser::
parseBlock/parseProcDecl's own MaxBlockDepth ceiling (ParseDecl.cpp,
issue #64) has -- despite walking the identical nested-procedure
structure a second time, once parsing has already accepted it.
checkProcBody calls checkBlock for the procedure's own block, which then
calls checkProcBody again for each nested procedure/function in
Block.Procs -- an entirely unguarded mutual recursion, parallel to
issue #596's (Sema::resolveType/resolveTypeImpl) but in the declaration/
scope-checking subsystem rather than type resolution, and reachable at a
dramatically smaller, more "ordinary" stack size (~2.5-3 MiB, per the
issue's own bisection) than any of this family's other gaps, because
checkBlock's own per-activation state (a BeforePop function_ref, a
moved-and-restored CurrentBlockLabels container, several lambdas, and
more) costs far more real stack per frame than resolveTypeImpl's or
checkExpr's.

Fixed by giving Sema a dedicated BlockDepth/MaxBlockDepth=500 term-count
ceiling (mirroring the parser's own, for a friendlier diagnostic on an
ordinary large-enough stack) PLUS a plang::stackNearlyExhausted
(StackBaseline) check, at the top of BOTH checkBlock and checkProcBody
(a separate ProcBodyDepth counter for checkProcBody's own half -- see
that member's comment in Sema.h for why it is not shared with
BlockDepth).

This file is the false-positive half of that fix's regression coverage:
499 levels of nested procedure declarations (the issue's own repro
depth, one under the parser's own MaxBlockDepth=500 ceiling) must still
compile and run cleanly on a normal (default, ~8MiB) stack -- the new
guard must not narrow what already-legitimate, boundary-exact input
this compiler accepts. The stack-exhaustion crash itself needs a
constrained ulimit to reproduce, which lit's own internal RUN-line shell
cannot script portably -- see a-499-level-nested-pointer-type-at-the-
typedepth-boundary-still-compiles-cleanly.pas (SemaRobustness, issue
#596) for this suite's identical precedent and disclaimer. Verified
manually instead, both before and after this fix: before, this exact
490-level input (well under the 500-level ceiling checked in here)
segfaulted (SIGSEGV, no diagnostic, past parsing and into mutually-
recursive checkBlock/checkProcBody per gdb) at `ulimit -s 2560`; after,
it compiles cleanly at that same bound, and a deeper, deliberately-
over-the-ceiling input is rejected with a clean "nested too deeply"
diagnostic instead of crashing even at `ulimit -s 1024` -- never a raw
crash -- recorded in this fix's own PR.
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:ok
*)

program p;
procedure p0;
procedure p1;
procedure p2;
procedure p3;
procedure p4;
procedure p5;
procedure p6;
procedure p7;
procedure p8;
procedure p9;
procedure p10;
procedure p11;
procedure p12;
procedure p13;
procedure p14;
procedure p15;
procedure p16;
procedure p17;
procedure p18;
procedure p19;
procedure p20;
procedure p21;
procedure p22;
procedure p23;
procedure p24;
procedure p25;
procedure p26;
procedure p27;
procedure p28;
procedure p29;
procedure p30;
procedure p31;
procedure p32;
procedure p33;
procedure p34;
procedure p35;
procedure p36;
procedure p37;
procedure p38;
procedure p39;
procedure p40;
procedure p41;
procedure p42;
procedure p43;
procedure p44;
procedure p45;
procedure p46;
procedure p47;
procedure p48;
procedure p49;
procedure p50;
procedure p51;
procedure p52;
procedure p53;
procedure p54;
procedure p55;
procedure p56;
procedure p57;
procedure p58;
procedure p59;
procedure p60;
procedure p61;
procedure p62;
procedure p63;
procedure p64;
procedure p65;
procedure p66;
procedure p67;
procedure p68;
procedure p69;
procedure p70;
procedure p71;
procedure p72;
procedure p73;
procedure p74;
procedure p75;
procedure p76;
procedure p77;
procedure p78;
procedure p79;
procedure p80;
procedure p81;
procedure p82;
procedure p83;
procedure p84;
procedure p85;
procedure p86;
procedure p87;
procedure p88;
procedure p89;
procedure p90;
procedure p91;
procedure p92;
procedure p93;
procedure p94;
procedure p95;
procedure p96;
procedure p97;
procedure p98;
procedure p99;
procedure p100;
procedure p101;
procedure p102;
procedure p103;
procedure p104;
procedure p105;
procedure p106;
procedure p107;
procedure p108;
procedure p109;
procedure p110;
procedure p111;
procedure p112;
procedure p113;
procedure p114;
procedure p115;
procedure p116;
procedure p117;
procedure p118;
procedure p119;
procedure p120;
procedure p121;
procedure p122;
procedure p123;
procedure p124;
procedure p125;
procedure p126;
procedure p127;
procedure p128;
procedure p129;
procedure p130;
procedure p131;
procedure p132;
procedure p133;
procedure p134;
procedure p135;
procedure p136;
procedure p137;
procedure p138;
procedure p139;
procedure p140;
procedure p141;
procedure p142;
procedure p143;
procedure p144;
procedure p145;
procedure p146;
procedure p147;
procedure p148;
procedure p149;
procedure p150;
procedure p151;
procedure p152;
procedure p153;
procedure p154;
procedure p155;
procedure p156;
procedure p157;
procedure p158;
procedure p159;
procedure p160;
procedure p161;
procedure p162;
procedure p163;
procedure p164;
procedure p165;
procedure p166;
procedure p167;
procedure p168;
procedure p169;
procedure p170;
procedure p171;
procedure p172;
procedure p173;
procedure p174;
procedure p175;
procedure p176;
procedure p177;
procedure p178;
procedure p179;
procedure p180;
procedure p181;
procedure p182;
procedure p183;
procedure p184;
procedure p185;
procedure p186;
procedure p187;
procedure p188;
procedure p189;
procedure p190;
procedure p191;
procedure p192;
procedure p193;
procedure p194;
procedure p195;
procedure p196;
procedure p197;
procedure p198;
procedure p199;
procedure p200;
procedure p201;
procedure p202;
procedure p203;
procedure p204;
procedure p205;
procedure p206;
procedure p207;
procedure p208;
procedure p209;
procedure p210;
procedure p211;
procedure p212;
procedure p213;
procedure p214;
procedure p215;
procedure p216;
procedure p217;
procedure p218;
procedure p219;
procedure p220;
procedure p221;
procedure p222;
procedure p223;
procedure p224;
procedure p225;
procedure p226;
procedure p227;
procedure p228;
procedure p229;
procedure p230;
procedure p231;
procedure p232;
procedure p233;
procedure p234;
procedure p235;
procedure p236;
procedure p237;
procedure p238;
procedure p239;
procedure p240;
procedure p241;
procedure p242;
procedure p243;
procedure p244;
procedure p245;
procedure p246;
procedure p247;
procedure p248;
procedure p249;
procedure p250;
procedure p251;
procedure p252;
procedure p253;
procedure p254;
procedure p255;
procedure p256;
procedure p257;
procedure p258;
procedure p259;
procedure p260;
procedure p261;
procedure p262;
procedure p263;
procedure p264;
procedure p265;
procedure p266;
procedure p267;
procedure p268;
procedure p269;
procedure p270;
procedure p271;
procedure p272;
procedure p273;
procedure p274;
procedure p275;
procedure p276;
procedure p277;
procedure p278;
procedure p279;
procedure p280;
procedure p281;
procedure p282;
procedure p283;
procedure p284;
procedure p285;
procedure p286;
procedure p287;
procedure p288;
procedure p289;
procedure p290;
procedure p291;
procedure p292;
procedure p293;
procedure p294;
procedure p295;
procedure p296;
procedure p297;
procedure p298;
procedure p299;
procedure p300;
procedure p301;
procedure p302;
procedure p303;
procedure p304;
procedure p305;
procedure p306;
procedure p307;
procedure p308;
procedure p309;
procedure p310;
procedure p311;
procedure p312;
procedure p313;
procedure p314;
procedure p315;
procedure p316;
procedure p317;
procedure p318;
procedure p319;
procedure p320;
procedure p321;
procedure p322;
procedure p323;
procedure p324;
procedure p325;
procedure p326;
procedure p327;
procedure p328;
procedure p329;
procedure p330;
procedure p331;
procedure p332;
procedure p333;
procedure p334;
procedure p335;
procedure p336;
procedure p337;
procedure p338;
procedure p339;
procedure p340;
procedure p341;
procedure p342;
procedure p343;
procedure p344;
procedure p345;
procedure p346;
procedure p347;
procedure p348;
procedure p349;
procedure p350;
procedure p351;
procedure p352;
procedure p353;
procedure p354;
procedure p355;
procedure p356;
procedure p357;
procedure p358;
procedure p359;
procedure p360;
procedure p361;
procedure p362;
procedure p363;
procedure p364;
procedure p365;
procedure p366;
procedure p367;
procedure p368;
procedure p369;
procedure p370;
procedure p371;
procedure p372;
procedure p373;
procedure p374;
procedure p375;
procedure p376;
procedure p377;
procedure p378;
procedure p379;
procedure p380;
procedure p381;
procedure p382;
procedure p383;
procedure p384;
procedure p385;
procedure p386;
procedure p387;
procedure p388;
procedure p389;
procedure p390;
procedure p391;
procedure p392;
procedure p393;
procedure p394;
procedure p395;
procedure p396;
procedure p397;
procedure p398;
procedure p399;
procedure p400;
procedure p401;
procedure p402;
procedure p403;
procedure p404;
procedure p405;
procedure p406;
procedure p407;
procedure p408;
procedure p409;
procedure p410;
procedure p411;
procedure p412;
procedure p413;
procedure p414;
procedure p415;
procedure p416;
procedure p417;
procedure p418;
procedure p419;
procedure p420;
procedure p421;
procedure p422;
procedure p423;
procedure p424;
procedure p425;
procedure p426;
procedure p427;
procedure p428;
procedure p429;
procedure p430;
procedure p431;
procedure p432;
procedure p433;
procedure p434;
procedure p435;
procedure p436;
procedure p437;
procedure p438;
procedure p439;
procedure p440;
procedure p441;
procedure p442;
procedure p443;
procedure p444;
procedure p445;
procedure p446;
procedure p447;
procedure p448;
procedure p449;
procedure p450;
procedure p451;
procedure p452;
procedure p453;
procedure p454;
procedure p455;
procedure p456;
procedure p457;
procedure p458;
procedure p459;
procedure p460;
procedure p461;
procedure p462;
procedure p463;
procedure p464;
procedure p465;
procedure p466;
procedure p467;
procedure p468;
procedure p469;
procedure p470;
procedure p471;
procedure p472;
procedure p473;
procedure p474;
procedure p475;
procedure p476;
procedure p477;
procedure p478;
procedure p479;
procedure p480;
procedure p481;
procedure p482;
procedure p483;
procedure p484;
procedure p485;
procedure p486;
procedure p487;
procedure p488;
procedure p489;
procedure p490;
procedure p491;
procedure p492;
procedure p493;
procedure p494;
procedure p495;
procedure p496;
procedure p497;
procedure p498;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
end;
begin
  writeln('ok');
end.
