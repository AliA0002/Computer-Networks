CXX=g++
CXXFLAGS= -g -Wall -fno-builtin -std=c++11 -Wno-deprecated-declarations -Wpedantic -I../common# --coverage
# List of source files for your file server
SOURCES = $(wildcard *.cpp)
EXE = miProxy

# Generate the names of the file server's object files
OBJS=${SOURCES:.cpp=.o}

USER := $(shell whoami)
ifeq ($(USER),kgurushankar)
	SUBMITCXX = g++-7
else 
	SUBMITCXX = g++
endif

# GCOV = ${SOURCES:.cpp=.cpp.gcov}

all: ${EXE}

# Compile the file server
# Note: No autotag here, only runs when submit is run
${EXE}: ${OBJS}
	${CXX} ${CXXFLAGS} -o $@ $^ ../common/*.o -pthread -ldl

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
	rm -f ${OBJS} ${EXE} ${SOURCEMDS} ${SOURCEPDFS} *.gc* allfiles.pdf *.tar.gz
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