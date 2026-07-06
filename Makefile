# Build helpers for the SearchEngine project.
# The real compilation still uses g++; this Makefile only records the
# module-specific commands so they are easier to run and remember.

CXX := g++
CXXFLAGS := -std=c++17
INCLUDE := -I../../include
SERVER_LIB_DIR := /usr/local/lib
SERVER_LDFLAGS := -L$(SERVER_LIB_DIR)
SERVER_LIBS := -lredis++ -lhiredis -llog4cpp -lpthread

.PHONY: help all offline module1 module2 server run-server client clean

help:
	@echo "SearchEngine build targets:"
	@echo "  make module1  - build offline dictionary generator"
	@echo "  make module2  - build offline page/index generator"
	@echo "  make run-server - build and run server with LD_LIBRARY_PATH"
	@echo "  make client   - build command line client"
	@echo "  make offline  - build module1 and module2"
	@echo "  make all      - build all modules"
	@echo "  make clean    - remove generated binaries"

all: module1 module2 server client

offline: module1 module2

module1:
	cd src/module1 && $(CXX) *.cc $(INCLUDE) $(CXXFLAGS) -o a.out

module2:
	cd src/module2 && $(CXX) *.cc $(INCLUDE) $(CXXFLAGS) -o a.out

server:
	@cd src/module3 && $(CXX) *.cc -o server $(INCLUDE) $(CXXFLAGS) $(SERVER_LDFLAGS) $(SERVER_LIBS)

run-server: server
	@cd src/module3 && \
	stopped=0; \
	stop_server() { \
		trap '' INT TERM; \
		if [ $$stopped -eq 0 ]; then \
			stopped=1; \
			echo "[Server] stopped by user"; \
		fi; \
		if [ -n "$$server_pid" ]; then \
			kill -TERM $$server_pid 2>/dev/null; \
			for _ in 1 2 3; do \
				if ! kill -0 $$server_pid 2>/dev/null; then \
					break; \
				fi; \
				sleep 1; \
			done; \
			if kill -0 $$server_pid 2>/dev/null; then \
				kill -KILL $$server_pid 2>/dev/null; \
			fi; \
			wait $$server_pid 2>/dev/null; \
		fi; \
		exit 0; \
	}; \
	trap stop_server INT TERM; \
	LD_LIBRARY_PATH=$(SERVER_LIB_DIR):$$LD_LIBRARY_PATH ./server & \
	server_pid=$$!; \
	wait $$server_pid; \
	status=$$?; \
	if [ $$status -eq 130 ] || [ $$status -eq 143 ]; then stop_server; fi; \
	exit $$status

client:
	@cd src/module4 && $(CXX) *.cc $(INCLUDE) $(CXXFLAGS) -o a.out

clean:
	rm -f src/module1/a.out
	rm -f src/module2/a.out
	rm -f src/module3/server
	rm -f src/module4/a.out
