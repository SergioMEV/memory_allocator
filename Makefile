CC := clang
CXX := clang++
CFLAGS := -g -Wall -Werror -fPIC

all: myallocator.so test/malloc-test

clean:
	rm -rf obj myallocator.so test/malloc-test

obj/allocator.o: allocator.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c -o obj/allocator.o allocator.c

myallocator.so: heaplayers/gnuwrapper.cpp heaplayers/wrapper.h obj/allocator.o
	$(CXX) -shared $(CFLAGS) -o myallocator.so heaplayers/gnuwrapper.cpp obj/allocator.o

zip:
	@zip -q -r malloc.zip . -x .git/\* .vscode/\* .clang-format .gitignore myallocator.so obj test
	@echo "Done."

format:
	@echo "Reformatting source code."
	@clang-format -i --style=file $(wildcard *.c) $(wildcard *.h)
	@echo "Done."

.PHONY: all clean zip format

