target:
	mkdir -p build
	gcc main.c -o build/main

clean:
	rm -rf build

run:
	./build/main