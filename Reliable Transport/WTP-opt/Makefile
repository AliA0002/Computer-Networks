CXX = g++
CXXFLAGS= -g -Wall -fno-builtin -std=c++11 -Wno-deprecated-declarations -Wpedantic# --coverage
HEADER = $(wildcard *.h)
SOURCES = $(wildcard *.cpp)
SENDER = wSender
RECEIVER = wReceiver

SOURCES := $(filter-out ${SENDER}.cpp, ${SOURCES})
SOURCES := $(filter-out ${RECEIVER}.old.cpp, ${SOURCES})
SOURCES := $(filter-out ${RECEIVER}.cpp, ${SOURCES})
# Generate the names of the file server's object files
OBJS:=${SOURCES:.cpp=.o}


USER := $(shell whoami)
ifeq ($(USER),kgurushankar)
	SUBMITCXX = g++-7
else 
	SUBMITCXX = g++
endif

# GCOV = ${SOURCES:.cpp=.cpp.gcov}

all: ${SENDER} ${RECEIVER}

# Compile the file server
# Note: No autotag here, only runs when submit is run
${SENDER}: ${OBJS} ${SENDER}.o
	${CXX} ${CXXFLAGS} -o $@ $^
	
${RECEIVER}: ${OBJS} ${RECEIVER}.o
	${CXX} ${CXXFLAGS} -o $@ $^

# Generic rules for compiling a source file to an object file
%.o: %.cpp
	${CXX} ${CXXFLAGS} -c $<
%.o: %.cc
	${CXX} ${CXXFLAGS} -c $<


SOURCEFILES = $(wildcard *.cpp)
SOURCEMDS = $(SOURCEFILES:.cpp=.cpp.md)
SOURCEPDFS = $(SOURCEFILES:.cpp=.cpp.pdf)

format: ${SOURCEFILES}
	clang-format -style=file -i $^ *.h

clean:
	rm -f ${SENDER}.o ${RECEIVER}.o ${SENDER} ${RECEIVER} ${OBJS} ${EXE} ${SOURCEMDS} ${SOURCEPDFS} *.gc* allfiles.pdf *.tar.gz output/* log*.txt
	rm -rf *.dSYM 

# I build the thread lib to ensure that I dont have a submission with compiler errors...
submit: CXX = ${SUBMITCXX}

submit: objects
	$(MAKE) clean

# using https://github.com/c4s4/md2pdf
# using poppler pdfunite

%.cpp.md: %.cpp
	echo '```cpp' > $@
	clang-format -style="{BasedOnStyle: llvm, Language: Cpp, ColumnLimit: 80}" $< >> $@
	echo >> $@
	echo '```' >> $@

%.cpp.pdf: %.cpp.md
	md2pdf $<


pdf: $(SOURCEPDFS)
	pdfunite $^ allfiles.pdf

.PHONY: submit