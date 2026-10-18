# Host build of the firmware core: unit tests and the simulator.
#
#   make test        run the unit tests
#   make sim         build the simulator (host/sim): the firmware's API and
#                    display engine behind a small HTTP server on :8080
#   make run-sim     build and start it; then in dashboard/:
#                    PUMPKIN_HOST=localhost:8080 npm run dev
#
# Needs a C++17 compiler and ArduinoJson 7: taken from PlatformIO's libdeps
# after a `pio run`, otherwise downloaded once into host/.deps.

CXX ?= g++
ARDUINOJSON_VERSION := 7.4.3
PIO_ARDUINOJSON := ../.pio/libdeps/esp32dev/ArduinoJson/src
DEPS := .deps

ifneq ($(wildcard $(PIO_ARDUINOJSON)/ArduinoJson.h),)
  JSON_INC := $(PIO_ARDUINOJSON)
  JSON_DEP :=
else
  JSON_INC := $(DEPS)
  JSON_DEP := $(DEPS)/ArduinoJson.h
endif

CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS := -I../src -I$(JSON_INC)

CORE_SRC := $(wildcard ../src/core/*.cpp)
TEST_SRC := $(wildcard ../tests/*.cpp)
SIM_SRC := sim.cpp http_server.cpp

all: test sim

$(DEPS)/ArduinoJson.h:
	mkdir -p $(DEPS)
	curl -sSL -o $@ https://github.com/bblanchon/ArduinoJson/releases/download/v$(ARDUINOJSON_VERSION)/ArduinoJson-v$(ARDUINOJSON_VERSION).h

build/tests: $(JSON_DEP) $(CORE_SRC) $(TEST_SRC) ../tests/test.h $(wildcard ../src/core/*.h)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -I../tests -o $@ $(CORE_SRC) $(TEST_SRC)

test: build/tests
	./build/tests

sim: $(JSON_DEP) $(CORE_SRC) $(SIM_SRC) $(wildcard ../src/core/*.h) http_server.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -o sim $(CORE_SRC) $(SIM_SRC)

run-sim: sim
	./sim

clean:
	rm -rf build sim

.PHONY: all test run-sim clean
