CXXFLAGS = -Wall
CXXFLAGS += -Werror
CXXFLAGS += -std=c++11
CXXFLAGS += -Wno-invalid-offsetof
CXXFLAGS += -Wpointer-arith
CXXFLAGS += -Wunused-macros
CXXFLAGS += -O3
CXXFLAGS += -I./

SRC=generate_traces.cpp\
	generate_keys.cpp\
    traces_tester.cpp

OBJS=$(SRC:.cpp=.o)

PROGS=traces_tester

TARGET = $(OBJS) $(PROGS)

all: $(TARGET)

%.o: %.cpp %.h Makefile 
	$(CXX) -MD -c -o $@ $(CXXFLAGS) $<

$(PROGS): $(OBJS)
	$(CXX) -o $@ $(OBJS)

clean:
	rm -rf *.o *.d $(PROGS)