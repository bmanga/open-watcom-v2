plustest_name = perl

!include testenv1.mif

!ifdef test1
test_flags = -d2
alt_error=01
!else
test_flags = -ox-d1
alt_error=00
!endif

test_flags += -w0 -DTEST

test_flags_386 =
test_flags_i86 = -mm
test_flags_axp =

stack_386=opt stack=32k
stack_i86=opt stack=16k
stack_axp=opt stack=32k

extra_run_tstio = <tstio.in

exes = &
    perltest.$(exe) &
    perlasso.$(exe) &
    tstio.$(exe)

test : .symbolic start clean start_test test.lnk $(exes) set_result
    @%make clean

set_result : .symbolic
    cat t2.out >>$(%ERROR_FILE)
    $(noecho)%make common_set_result

perltest.$(exe) : perltest.obj perlclas.obj regex.obj
perlasso.$(exe) : perlasso.obj perlclas.obj regex.obj
tstio.$(exe) : tstio.obj perlclas.obj regex.obj

start_test : .symbolic
    @%make common_start_test

clean : .procedure
    @%make echo_clean
    $(noecho)%make common_clean
