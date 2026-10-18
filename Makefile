#Thanks to https://stackoverflow.com/questions/52034997/how-to-make-makefile-recompile-when-a-header-file-is-changed for the -MMD & -MP flags
#Without them headers wouldn't trigger recompilation

#Bare `make` (used by docker/build.sh) must build libnavi.so
.DEFAULT_GOAL := audit-libs

#Force g++ cause clang crashes on some hooks
CXX := g++

libs := $(wildcard lib/*.a)
srcs := $(shell find src/ -type f -iname "*.cpp")

CXXFLAGS := -O3 -flto=auto -fPIC -m32 -std=c++20 -Wall -Wextra -Wpedantic -Wno-error=format-security -D_GLIBCXX_USE_CXX11_ABI=0
CXXFLAGS += -I./src -I./src/core -I./src/model -I./src/model/migrations -I./src/presenter -I./src/view -I./src/model/sdk

LDFLAGS := -shared -Wl,--no-undefined
LDFLAGS += $(shell pkg-config --libs "openssl")
CXXFLAGS += -I/usr/include/x86_64-linux-gnu
LDFLAGS += /usr/lib/i386-linux-gnu/libcurl.so.4

JOBS := $(shell nproc)

#DATE := $(shell date "+%Y%m%d%H%M%S")
DATE := $(shell grep '^Version:' res/version | cut -d' ' -f2 | xargs)

ifeq ($(TRACE),1)
	CXXFLAGS += -D "TRACE"
endif

ifeq ($(DEBUG),1)
	CXXFLAGS += -D "DEBUG" -g -O0
	CXXFLAGS := $(filter-out -O3,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -flto=auto,$(CXXFLAGS))
else
	CXXFLAGS += -Wno-unused-parameter -Wno-unused-variable
endif

ifeq ($(NATIVE),1)
	CXXFLAGS += -march=native
endif

export PATH := $(HOME)/.local/bin:$(PATH)

#Speed up compilation if additional dependencies are found
ifeq ($(shell type ccache &> /dev/null && echo "found"),found)
	export PATH := /usr/lib/ccache/bin:$(PATH)
endif
ifeq ($(shell type mold &> /dev/null && echo "found"),found)
	LDFLAGS += -fuse-ld=mold
else ifeq ($(shell type ld.gold &> /dev/null && echo "found"),found)
	LDFLAGS += -fuse-ld=gold
endif

COMPILER_VERSION := $(shell $(CXX) -dumpfullversion -dumpversion)
FLAGSSHA := $(shell echo "$(CXX) $(COMPILER_VERSION) & $(CXXFLAGS) & $(LDFLAGS)" | sha256sum | cut -d " " -f 1)
NAVISO := bin/libnavi-$(FLAGSSHA).so
objs := $(srcs:src/%.cpp=obj/$(FLAGSSHA)/%.o)
deps = $(objs:%.o=%.d) $(GTEST_OBJS:%.o=%.d) $(UNIT_TEST_OBJS:%.o=%.d) $(COV_PROD_OBJS:%.o=%.d)

GTEST_DIR := third_party/googletest/googletest
UNIT_TEST_SRCS := $(wildcard tests/unit/*.cpp)
UNIT_TEST_OBJS := $(UNIT_TEST_SRCS:tests/unit/%.cpp=obj/$(FLAGSSHA)/tests/unit/%.o)
GTEST_OBJS := obj/$(FLAGSSHA)/gtest/gtest-all.o obj/$(FLAGSSHA)/gtest/gtest_main.o

UNIT_TEST_PROD_SRCS := \
	src/core/curl.cpp \
	src/core/decompiler.cpp \
	src/core/filewatcher.cpp \
	src/core/memhlp.cpp \
	src/core/patterns.cpp \
	src/core/utils.cpp \
	src/core/vftableinfo.cpp \
	src/core/zip.cpp \
	src/model/config.cpp \
	src/model/depotkeys.cpp \
	src/model/lua_parser.cpp \
	src/model/manifestcode.cpp \
	src/model/paths.cpp \
	src/model/vdf.cpp \
	src/model/migrations/cache_migration.cpp \
	src/model/migrations/config_migration.cpp \
	src/model/migrations/legacy_migration.cpp \
	src/model/sdk/binarykv.cpp \
	src/model/sdk/CNetPacket.cpp \
	src/model/sdk/steam.cpp \
	src/model/sdk/protobufs/encrypted_app_ticket.pb.cpp \
	src/model/sdk/protobufs/enums.pb.cpp \
	src/model/sdk/protobufs/enums_clientserver.pb.cpp \
	src/model/sdk/protobufs/steammessages_base.pb.cpp \
	src/model/sdk/protobufs/steammessages_clientserver.pb.cpp \
	src/model/sdk/protobufs/steammessages_clientserver_2.pb.cpp \
	src/presenter/apps.cpp \
	src/presenter/autoupdate.cpp \
	src/presenter/luagenerator.cpp \
	src/presenter/nativeinstall.cpp

UNIT_TEST_PROD_OBJS := $(UNIT_TEST_PROD_SRCS:src/%.cpp=obj/$(FLAGSSHA)/%.o)
UNIT_TEST_SDK_SRCS := $(filter src/model/sdk/%,$(UNIT_TEST_PROD_SRCS))
UNIT_TEST_SDK_OBJS := $(UNIT_TEST_SDK_SRCS:src/%.cpp=obj/$(FLAGSSHA)/%.o)

COV_PROD_SRCS := $(filter-out src/model/sdk/%,$(UNIT_TEST_PROD_SRCS))
COV_PROD_OBJS := $(COV_PROD_SRCS:src/%.cpp=obj/cov/%.o)
COV_CXXFLAGS := $(filter-out -O3 -flto=auto,$(CXXFLAGS)) -O0 -g --coverage

obj/$(FLAGSSHA)/gtest/%.o: $(GTEST_DIR)/src/%.cc
	@mkdir -p $(dir $@)
	$(CXX) -m32 -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0 -isystem $(GTEST_DIR)/include -I$(GTEST_DIR) -pthread -MMD -MP -c $< -o $@

obj/$(FLAGSSHA)/tests/unit/%.o: tests/unit/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isystem $(GTEST_DIR)/include -isysteminclude -MMD -MP -c $< -o $@

obj/cov/model/config.o: src/model/config.cpp res/config.yaml
	$(shell ./embed-config.sh)
	@mkdir -p $(dir $@)
	$(CXX) $(COV_CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

obj/cov/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COV_CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

bin/unit_tests: $(UNIT_TEST_OBJS) $(UNIT_TEST_PROD_OBJS) $(GTEST_OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -fuse-ld=gold $^ lib/libprotobuf.a lib/libyaml-cpp.a -lssl -lcrypto /usr/lib/i386-linux-gnu/libcurl.so.4 -lpthread -ldl -o $@
	chmod 755 $@

bin/unit_tests_cov: $(COV_PROD_OBJS) $(UNIT_TEST_SDK_OBJS) $(UNIT_TEST_OBJS) $(GTEST_OBJS)
	@mkdir -p bin
	$(CXX) $(COV_CXXFLAGS) -fuse-ld=gold $^ lib/libprotobuf.a lib/libyaml-cpp.a -lssl -lcrypto /usr/lib/i386-linux-gnu/libcurl.so.4 -lpthread -ldl -o $@
	chmod 755 $@

test: bin/unit_tests
	./bin/unit_tests
	if [ -d tests/integration ]; then pytest -q tests/integration; fi
	$(MAKE) coverage

coverage: bin/unit_tests_cov
	./bin/unit_tests_cov
	PATH="$(HOME)/.local/bin:$(PATH)" gcovr -r src obj/cov

test-flow:
	pytest -q --live tests/integration/test_steam_ui_flow.py

test-all: test test-flow

audit-libs:
	$(MAKE) -j $(JOBS) $(NAVISO) bin/library-inject.so
	$(MAKE) link-bins

tools:
	$(MAKE) -j 2 schema-grabber ticket-grabber

link-bins:
	-test -f "$(NAVISO)" && ln -f "$(NAVISO)" "bin/libnavi.so"
	-test -f "tools/library-inject/library-inject.so" && ln -f "tools/library-inject/library-inject.so" "bin/library-inject.so"

$(NAVISO): $(objs) $(libs)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $^ -o $(NAVISO) $(LDFLAGS)
	$(MAKE) link-bins

bin/library-inject.so:
	@mkdir -p bin
	$(MAKE) -C tools/library-inject
	$(MAKE) link-bins

schema-grabber:
	$(MAKE) -C tools/schema-grabber

ticket-grabber:
	$(MAKE) -C tools/ticket-grabber

-include $(deps)
obj/$(FLAGSSHA)/presenter/update.o: src/presenter/update.cpp res/version
	$(shell ./embed-version.sh)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

obj/$(FLAGSSHA)/model/config.o: src/model/config.cpp res/config.yaml
	$(shell ./embed-config.sh)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

obj/$(FLAGSSHA)/%.o : src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

clean-libs:
	rm -rvf \
		"obj/" \
		"bin/" \
		"zips/" \
		"tools/ticket-grabber/bin" \
		"tools/ticket-grabber/obj" \
		"tools/schema-grabber/bin" \
		"tools/schema-grabber/obj"

clean-tools:
	-$(MAKE) -C tools/schema-grabber clean
	-$(MAKE) -C tools/ticket-grabber clean

install:
	./install.sh install

uninstall:
	./install.sh uninstall

zips: build
	@mkdir -p zips
	7z a -mx9 -m9=lzma2 \
		"zips/navi $(DATE).7z" \
		"bin/libnavi.so" \
		"bin/library-inject.so" \
		"install.sh" \
		"scripts/lib" \
		"docs/LICENSE" \
		"res/config.yaml" \
		"tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish/ticket-grabber" \
		"tools/schema-grabber/bin/Release/net9.0/linux-x64/publish/schema-grabber"

	#Compatibility for Github issues
	7z a -mx9 -m9=lzma \
		"zips/navi $(DATE).zip" \
		"bin/libnavi.so" \
		"bin/library-inject.so" \
		"install.sh" \
		"scripts/lib" \
		"docs/LICENSE" \
		"res/config.yaml" \
		"tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish/ticket-grabber" \
		"tools/schema-grabber/bin/Release/net9.0/linux-x64/publish/schema-grabber"

zips-config:
	7z a -mx9 -m9=lzma "zips/navi - NaviConfig $(DATE).zip" "$(HOME)/.config/navi/config.yaml"
	#Compatibility for Github issues
	7z a -mx9 -m9=lzma2 "zips/navi - NaviConfig $(DATE).7z" "$(HOME)/.config/navi/config.yaml"


clean: clean-libs clean-tools
build: audit-libs tools
rebuild: clean build
release: rebuild zips

.PHONY: \
	audit-libs \
	tools \
	link-bins \
	schema-grabber \
	ticket-grabber \
	clean-libs \
	clean-tools \
	install \
	uninstall \
	zips \
	zips-config \
	clean \
	build \
	rebuild \
	release \
	test \
	test-flow \
	test-all \
	coverage
