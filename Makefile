CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I./backend/include -I/opt/homebrew/include $(shell mysql_config --cflags)
LDFLAGS ?= -L/opt/homebrew/lib $(shell mysql_config --libs)

CORE_SRC = backend/src/logging/Logger.cpp \
           backend/src/models/User.cpp \
           backend/src/models/Account.cpp \
           backend/src/models/Transaction.cpp \
           backend/src/database/DatabaseManager.cpp \
           backend/src/transaction/TransactionManager.cpp \
           backend/src/concurrency/ConcurrentTransactionProcessor.cpp \
           backend/src/concurrency/ConcurrencyManager.cpp \
           backend/src/locking/LockManager.cpp \
           backend/src/deadlock/DeadlockDetector.cpp \
           backend/src/recovery/RecoveryManager.cpp \
           backend/src/resources/ResourceManager.cpp

MAIN_SRC = backend/src/main.cpp $(CORE_SRC)
TEST_SRC = tests/test_db.cpp $(CORE_SRC)

TARGET = paylock
TEST_TARGET = test_db

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(MAIN_SRC)
	$(CXX) $(CXXFLAGS) $(MAIN_SRC) $(LDFLAGS) -o $(TARGET)

$(TEST_TARGET): $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) $(LDFLAGS) -o $(TEST_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)
	rm -rf build

.PHONY: all clean test run
