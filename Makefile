CXX:=i686-w64-mingw32-g++
CXXFLAGS:=-O4 -Wall -I faad2-2.8.8/include
LDFLAGS:=-L faad2-2.8.8/libfaad/.libs
LIBS=-lfaad

.PHONY: all

all: aacdmsplit

aacdmsplit.exe: aacdmsplit

aacdmsplit: aacdmsplit.cc aacdmsplit.h
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(LIBS) -o $@ -s --static

clean:
	rm -rf aacdmsplit

aacdmsplit.tar.gz: aacdmsplit.h aacdmsplit.cc faad2.patch Makefile aacdmsplit.exe License README.md
	tar czvf $@ $^

archive: aacdmsplit.tar.gz
