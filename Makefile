# This makefile is only used to run tests.

CXX := clang++
FLAGS := -std=c++23 -Wall -Wextra -Wdeprecated -Wextra-semi -Wimplicit-fallthrough -Wconversion -Wno-implicit-int-float-conversion -Iinclude
TESTS := base all_pass

EXT_EXE :=

override semicolon := ;

all: compile_flags.txt

compile_flags.txt: Makefile
	$(file >$@)
	$(file >>$@,-DEM_ENABLE_TESTS=)
	$(file >>$@,-DEM_MINITEST_IMPLEMENTATION=)
	$(foreach x,$(FLAGS),$(file >>$@,$x))
	@true

%/:
	mkdir -p $@

$(foreach x,$(TESTS),\
	$(eval all: test/output/$x.txt)\
	$(eval test/build/$x$(EXT_EXE): test/$x.cpp include/em/minitest.hpp | test/build/ ; $(CXX) $(FLAGS) -Werror $$< -o $$@)\
	$(eval test/output/$x.txt: test/build/$x$(EXT_EXE) | test/output/ ; $$< >$$@ 2>&1 $$(semicolon) echo "--- EXIT CODE $$$$?" >>$$@)\
)

clear:
	rm -rf test/build test/output
