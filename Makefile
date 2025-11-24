CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I. -Wno-sign-compare
SRCS := main.cpp 
OBJS := $(SRCS:.cpp=.o)
TARGET := proc_scan

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)