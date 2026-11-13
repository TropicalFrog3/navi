#Thanks to https://stackoverflow.com/questions/52034997/how-to-make-makefile-recompile-when-a-header-file-is-changed for the -MMD & -MP flags
#Without them headers wouldn't trigger recompilation

#Bare `make` (used by docker/build.sh) must build libnavi.so
.DEFAULT_GOAL := audit-libs

#Force g++ cause clang crashes on some hooks
CXX := g++

libs := $(wildcard lib/*.a)
srcs := $(shell find src/ -type f -iname "*.cpp")

CXXFLAGS := -MMD -MP -O3 -flto=auto -fPIC -m32 -std=c++20 -Wall -Wextra -Wpedantic -Wno-error=format-security -D_GLIBCXX_USE_CXX11_ABI=0
CXXFLAGS += -I./src -I./src/core -I./src/model -I./src/model/migrations -I./src/presenter -I./src/view -I./src/model/sdk

LDFLAGS := -shared -Wl,--no-undefined
LDFLAGS += $(shell pkg-config --libs "openssl")
CXXFLAGS += -I/usr/include/x86_64-linux-gnu

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
	src/model/achievement_merger.cpp \
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
	src/model/sdk/IClientAppManager.cpp \
	src/presenter/apps.cpp \
	src/presenter/fakeappid.cpp \
	src/presenter/autoupdate.cpp \
	src/presenter/autocrack_manager.cpp \
	src/presenter/cloudredirect.cpp \
	src/core/cloud_oauth.cpp \
	src/presenter/cloud_manager.cpp \
	src/presenter/cloud_pending_ops.cpp \
	src/presenter/prefix_bucket_sync.cpp \
	src/view/cloud_view.cpp \
	src/presenter/luagenerator.cpp \
	src/presenter/nativeinstall.cpp \
	src/presenter/process_lock.cpp \
	src/view/storeinject_view.cpp

UNIT_TEST_PROD_OBJS := $(UNIT_TEST_PROD_SRCS:src/%.cpp=obj/$(FLAGSSHA)/%.o)
UNIT_TEST_SDK_SRCS := $(filter src/model/sdk/%,$(UNIT_TEST_PROD_SRCS))
UNIT_TEST_SDK_OBJS := $(UNIT_TEST_SDK_SRCS:src/%.cpp=obj/$(FLAGSSHA)/%.o)

COV_PROD_SRCS := $(filter-out src/model/sdk/%,$(UNIT_TEST_PROD_SRCS))
COV_PROD_OBJS := $(COV_PROD_SRCS:src/%.cpp=obj/cov/%.o)
COV_CXXFLAGS := $(filter-out -O3 -flto=auto,$(CXXFLAGS)) -O0 -g --coverage

obj/$(FLAGSSHA)/gtest/%.o: $(GTEST_DIR)/src/%.cc
	@mkdir -p $(dir $@)
	$(CXX) -m32 -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0 -isystem $(GTEST_DIR)/include -I$(GTEST_DIR) -pthread -c $< -o $@

obj/$(FLAGSSHA)/tests/unit/%.o: tests/unit/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isystem $(GTEST_DIR)/include -isysteminclude -c $< -o $@

obj/cov/model/config.o: src/model/config.cpp res/config.yaml
	$(shell ./scripts/lib/build.sh embed-config)
	@mkdir -p $(dir $@)
	$(CXX) $(COV_CXXFLAGS) -isysteminclude -c $< -o $@

obj/cov/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(COV_CXXFLAGS) -isysteminclude -c $< -o $@

bin/unit_tests: $(UNIT_TEST_OBJS) $(UNIT_TEST_PROD_OBJS) $(GTEST_OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -fuse-ld=gold $^ lib/libprotobuf.a lib/libyaml-cpp.a -lssl -lcrypto -lpthread -ldl -o $@
	chmod 755 $@

bin/unit_tests_cov: $(COV_PROD_OBJS) $(UNIT_TEST_SDK_OBJS) $(UNIT_TEST_OBJS) $(GTEST_OBJS)
	@mkdir -p bin
	$(CXX) $(COV_CXXFLAGS) -fuse-ld=gold $^ lib/libprotobuf.a lib/libyaml-cpp.a -lssl -lcrypto -lpthread -ldl -o $@
	chmod 755 $@

obj/$(FLAGSSHA)/tools/cloud_pending/%.o: tools/cloud_pending/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -c $< -o $@

bin/navi-cloud-apply-pending: obj/$(FLAGSSHA)/tools/cloud_pending/cloud_pending_main.o obj/$(FLAGSSHA)/presenter/cloud_pending_ops.o obj/$(FLAGSSHA)/presenter/cloud_manager.o obj/$(FLAGSSHA)/presenter/cloudredirect.o obj/$(FLAGSSHA)/model/paths.o obj/$(FLAGSSHA)/model/vdf.o obj/$(FLAGSSHA)/core/utils.o obj/$(FLAGSSHA)/core/log.o
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -fuse-ld=gold $^ lib/libyaml-cpp.a -lssl -lcrypto -lpthread -ldl -o $@
	chmod 755 $@

test: prepare_dirs bin/unit_tests
	./bin/unit_tests
	if [ -d tests/integration ]; then pytest -q tests/integration; fi
	$(MAKE) coverage

coverage: bin/unit_tests_cov
	./bin/unit_tests_cov
	PATH="$(HOME)/.local/bin:$(PATH)" gcovr --gcov-ignore-parse-errors=negative_hits.warn_once_per_file -r src obj/cov

test-integration:
	pytest -q tests/integration

test-gate07:
	./tests/integration/run_gate07_bwrap.sh

test-flow:
	pytest -q --live tests/integration/test_steam_ui_flow.py

test-ui:
	pytest -q tests/integration/ui

test-ui-p0:
	pytest -q -m ui_p0 tests/integration/ui

test-ui-a11y:
	pytest -q -m "a11y or responsive" tests/integration/ui

test-all: test

bundle-library:
	python3 tools/bundle-app-details.py

bundle-cloud:
	python3 tools/bundle-cloud-manager.py

bundle-core:
	python3 tools/bundle-navi-inject.py

bundle-lua:
	python3 tools/bundle-import-lua.py

bundle-depots:
	python3 tools/bundle-depot-selection.py

bundle-store:
	python3 tools/bundle-store-page.py

bundle-scripts: bundle-library bundle-cloud bundle-core bundle-lua bundle-depots bundle-store

audit-libs: bundle-scripts
	$(MAKE) -j $(JOBS) $(NAVISO) bin/library-inject.so
	$(MAKE) link-bins

tools:
	$(MAKE) -j 2 schema-grabber ticket-grabber

link-bins:
	-test -f "$(NAVISO)" && ln -f "$(NAVISO)" "bin/libnavi.so"
	-test -f "tools/library-inject/library-inject.so" && ln -f "tools/library-inject/library-inject.so" "bin/library-inject.so"

$(NAVISO): $(objs) $(libs) | prepare_dirs
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(objs) $(libs) -o $(NAVISO) $(LDFLAGS)
	$(MAKE) link-bins

bin/library-inject.so:
	@mkdir -p bin
	$(MAKE) -C tools/library-inject
	$(MAKE) link-bins

schema-grabber:
	$(MAKE) -C tools/schema-grabber

ticket-grabber:
	$(MAKE) -C tools/ticket-grabber

autocrack:
	@mkdir -p pkg/autocrack
	dotnet publish third_party/SteamAutoCrack/SteamAutoCrack.CLI/SteamAutoCrack.CLI.csproj \
		-c Release -r linux-x64 --self-contained \
		-p:PublishSingleFile=false -o pkg/autocrack/
	@if [ ! -d "pkg/autocrack/Goldberg" ]; then \
		echo "Downloading Goldberg emulator files..."; \
		pkg/autocrack/SteamAutoCrack.CLI downloademu || true; \
	fi

-include $(deps)
obj/$(FLAGSSHA)/presenter/update.o: src/presenter/update.cpp res/version
	$(shell ./scripts/lib/build.sh embed-version)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -c $< -o $@

obj/$(FLAGSSHA)/model/config.o: src/model/config.cpp res/config.yaml
	$(shell ./scripts/lib/build.sh embed-config)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -c $< -o $@

obj/$(FLAGSSHA)/%.o : src/%.cpp
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -isysteminclude -c $< -o $@

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

prepare_dirs:
	@mkdir -p bin
	@mkdir -p obj/$(FLAGSSHA)

.PHONY: prepare_dirs

uninstall:
	./install.sh uninstall

CLOUDREDIRECT_SO_HASH := c55ea63a02643e0df83b50241fc36a95c2a47fef8f71416479767f3b57d7c64d
CLOUDREDIRECT_CLI_HASH := 839398e7da55bd8830dabf623542c46e74d0f1fea4cf551601a244965f89782d
CLOUDREDIRECT_SO_SIZE := 2192860
CLOUDREDIRECT_CLI_SIZE := 13660

check-cloudredirect:
	@echo "Checking CloudRedirect prebuilt binaries..."
	@test -f res/cloud_redirect.so || (echo "ERROR: res/cloud_redirect.so missing"; exit 1)
	@test -f res/cloud_redirect_cli || (echo "ERROR: res/cloud_redirect_cli missing"; exit 1)
	@test "$$(sha256sum res/cloud_redirect.so | awk '{print $$1}')" = "$(CLOUDREDIRECT_SO_HASH)" || (echo "ERROR: SHA256 mismatch for res/cloud_redirect.so"; exit 1)
	@test "$$(sha256sum res/cloud_redirect_cli | awk '{print $$1}')" = "$(CLOUDREDIRECT_CLI_HASH)" || (echo "ERROR: SHA256 mismatch for res/cloud_redirect_cli"; exit 1)
	@test "$$(stat -c %s res/cloud_redirect.so)" = "$(CLOUDREDIRECT_SO_SIZE)" || (echo "ERROR: Size mismatch for res/cloud_redirect.so"; exit 1)
	@test "$$(stat -c %s res/cloud_redirect_cli)" = "$(CLOUDREDIRECT_CLI_SIZE)" || (echo "ERROR: Size mismatch for res/cloud_redirect_cli"; exit 1)
	@readelf -h res/cloud_redirect.so | grep -q "ELF32" || (echo "ERROR: res/cloud_redirect.so is not ELF32"; exit 1)
	@readelf -h res/cloud_redirect_cli | grep -q "ELF32" || (echo "ERROR: res/cloud_redirect_cli is not ELF32"; exit 1)
	@readelf -h res/cloud_redirect.so | grep -q "Intel 80386" || (echo "ERROR: res/cloud_redirect.so is not Intel 80386"; exit 1)
	@readelf -h res/cloud_redirect_cli | grep -q "Intel 80386" || (echo "ERROR: res/cloud_redirect_cli is not Intel 80386"; exit 1)
	@echo "OK: CloudRedirect prebuilt binaries verified (hashes, sizes, ELF32 architecture, co-located in res/)."

zips: build check-cloudredirect
	@mkdir -p zips
	7z a -mx9 -m9=lzma2 \
		"zips/navi $(DATE).7z" \
		"bin/libnavi.so" \
		"bin/library-inject.so" \
		"install.sh" \
		"scripts/lib" \
		"docs/LICENSE" \
		"docs/THIRD_PARTY.md" \
		"res/config.yaml" \
		"res/cloud_redirect.so" \
		"res/cloud_redirect_cli" \
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
		"docs/THIRD_PARTY.md" \
		"res/config.yaml" \
		"res/cloud_redirect.so" \
		"res/cloud_redirect_cli" \
		"tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish/ticket-grabber" \
		"tools/schema-grabber/bin/Release/net9.0/linux-x64/publish/schema-grabber"

zips-config:
	7z a -mx9 -m9=lzma "zips/navi - NaviConfig $(DATE).zip" "$(HOME)/.config/navi/config.yaml"
	#Compatibility for Github issues
	7z a -mx9 -m9=lzma2 "zips/navi - NaviConfig $(DATE).7z" "$(HOME)/.config/navi/config.yaml"


clean: clean-libs clean-tools
build: bundle-scripts audit-libs tools
rebuild: clean build
release: rebuild zips

.PHONY: \
	bundle-scripts \
	bundle-library \
	bundle-cloud \
	bundle-core \
	bundle-lua \
	bundle-depots \
	bundle-store \
	audit-libs \
	tools \
	link-bins \
	schema-grabber \
	ticket-grabber \
	autocrack \
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
	check-cloudredirect \
	test \
	test-integration \
	test-gate07 \
	test-flow \
	test-e2e \
	test-ui \
	test-ui-p0 \
	test-ui-a11y \
	test-all \
	coverage
