# This makefile is only used to run tests.

CXX := clang++
FLAGS := -std=c++23 -Wall -Wextra -Wdeprecated -Wextra-semi -Wimplicit-fallthrough -Wconversion -Wno-implicit-int-float-conversion -Iinclude
TESTS := \
	all_pass \
	base \
	base_noex,base,-fno-exceptions \

EXT_EXE :=

# Used to create local variables in a safer way. E.g. `$(call var,x := 42)`.
override var = $(eval override $(subst #,$$(strip #),$(subst $,$$$$,$1)))

override comma := ,
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
	$(call var,params := $(subst $(comma), ,$x))\
	$(call var,in_filename := $(if $(word 2,$(params)),$(word 2,$(params)),$(firstword $(params))))\
	$(call var,out_filename := $(firstword $(params)))\
	$(call var,flags := $(wordlist 3,$(words $(params)),$(params)))\
	$(eval all: test/output/$(out_filename).txt)\
	$(eval test/build/$(out_filename)$(EXT_EXE): test/$(in_filename).cpp include/em/minitest.hpp | test/build/ ; $(CXX) $(FLAGS) -Werror $(flags) $$< -o $$@)\
	$(eval test/output/$(out_filename).txt: test/build/$(out_filename)$(EXT_EXE) | test/output/ ; $$< >$$@ 2>&1 $$(semicolon) echo "--- EXIT CODE $$$$?" >>$$@)\
)

clear:
	rm -rf test/build test/output
