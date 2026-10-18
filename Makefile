# ClubManager —— 社团管理系统
# 数据结构：哈希表、AVL、二叉堆、栈、队列、图
CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude
BUILD := build

SRCS := src/hash_map.cpp src/avl.cpp src/friend_graph.cpp src/inverted_index.cpp src/system.cpp src/web_server.cpp src/main.cpp
OBJS := $(BUILD)/hash_map.o $(BUILD)/avl.o $(BUILD)/friend_graph.o $(BUILD)/inverted_index.o $(BUILD)/system.o $(BUILD)/web_server.o $(BUILD)/main.o
TEST_OBJS := $(BUILD)/hash_map.o $(BUILD)/avl.o $(BUILD)/friend_graph.o $(BUILD)/inverted_index.o $(BUILD)/system.o

.PHONY: all clean test demo gui

all: $(BUILD)/club.exe

$(BUILD)/club.exe: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ -lws2_32

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(BUILD)/tests.exe
	./$(BUILD)/tests.exe

$(BUILD)/tests.exe: tests/test_main.cpp $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) tests/test_main.cpp $(TEST_OBJS) -o $@

demo: $(BUILD)/club.exe
	./$(BUILD)/club.exe demo

gui: $(BUILD)/club.exe
	./$(BUILD)/club.exe gui

clean:
	rm -rf $(BUILD) club_data.txt
