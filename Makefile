NAME := allocator

target: clean
	mkdir -p build
	gcc allocator.c main.c -o build/$(NAME)

clean:
	rm -rf build

run:
	./build/$(NAME)