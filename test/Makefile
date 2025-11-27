
build/dninterp: dninterp.c ../src/cbor.c | build
	gcc  -coverage -fsanitize=address -g -O0 -I ../src $^ -o $@
	#cc  -fsanitize=undefined -g -O0 -I ../src $^ -o $@
	#arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mfp16-format=ieee -Os -g -c -I ../src ../src/cbor.c

build/test_cbor: ../src/cbor.c test_cbor.c | build
	cc -I ../src $^ -o $@

build/test_cobs: ../src/cobs.c test_cobs.c | build
	cc -I ../src $^ -o $@

build:
	mkdir $@

test: build/dninterp tests.dn
	build/dninterp tests.dn
	gcov build/dninterp-cbor.gcno

test-cbor: build/test_cbor
	build/test_cbor

test-cobs: build/test_cobs
	build/test_cobs

clean:
	rm -rf build
	rm -f *.gcov
