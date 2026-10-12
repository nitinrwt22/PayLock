CXX ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I./src -I/opt/homebrew/include $(shell mysql_config --cflags)
LDFLAGS ?= -L/opt/homebrew/lib $(shell mysql_config --libs)

SRC = src/database/DatabaseManager.cpp src/transaction/TransactionManager.cpp src/concurrency/ConcurrentTransactionProcessor.cpp
MAIN_SRC = src/main.cpp $(SRC)
TEST_SRC = tests/test_db.cpp $(SRC)

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
