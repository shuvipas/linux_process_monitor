CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I. -Wno-sign-compare
SRCS := system_monitor.cpp 
OBJS := $(SRCS:.cpp=.o)
TARGET := system_monitor

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)