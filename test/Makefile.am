EXTRA_DIST = \
        ClientTest.h \
        ClientTest.cpp \
	COPYING \
        client-test-main.cpp \
	test.h \
        Algorithm/Artistic \
        Algorithm/COPYING \
        Algorithm/Diff.pm \
        Algorithm/README \
        testcases/ical20.ics \
        testcases/itodo20.ics \
        testcases/vcal10.ics \
        testcases/vcard21.vcf \
        testcases/vcard30.vcf \
        testcases/imemo20.ics \
 		$(wildcard testcases/ical20.ics.*.patch) \
		$(wildcard testcases/itodo20.ics.*.patch) \
		$(wildcard testcases/vcal10.ics.*.patch) \
		$(wildcard testcases/vcard21.vcf.*.patch) \
		$(wildcard testcases/vcard30.vcf.*.patch) \
		$(wildcard testcases/imemeo20.ics.*.patch) \
        synccompare.pl

PATCH_FILES = $(addsuffix .patch,$(wildcard testcases/*.tem)) 
.PHONY: testcase2patch
testcase2patch: $(PATCH_FILES)
#rules when the server specific file changes
$(addsuffix .patch,testcases/%.tem) : testcases/%.tem
	diff -u `echo $< | rev| cut -d '.' -f 3- | rev` $< >$@ ; echo ""

#rules when the general case file changes
$(addsuffix .patch,testcases/%.funambol.tem) : testcases/%
	diff -u $< `echo $@| rev| cut -d '.' -f 2-| rev` >$@ ; echo ""
$(addsuffix .patch,testcases/%.synthesis.tem) : testcases/%
	diff -u $< `echo $@| rev| cut -d '.' -f 2-| rev` >$@ ; echo ""
$(addsuffix .patch,testcases/%.scheduleworld.tem) : testcases/%
	diff -u $< `echo $@| rev| cut -d '.' -f 2-| rev` >$@ ; echo ""

