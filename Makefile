monitor: main.o process.o cpu.o memory.o disk.o system.o ui.o
	g++ main.o process.o cpu.o memory.o disk.o system.o ui.o -o monitor

main.o: src/main.cpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/main.cpp -o main.o

process.o: src/process.cpp include/process.hpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/process.cpp -o process.o

cpu.o: src/cpu.cpp include/cpu.hpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/cpu.cpp -o cpu.o

memory.o: src/memory.cpp include/memory.hpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/memory.cpp -o memory.o

disk.o: src/disk.cpp include/disk.hpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/disk.cpp -o disk.o

system.o: src/system.cpp include/system.hpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/system.cpp -o system.o

ui.o: src/ui.cpp include/ui.hpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -MMD -MP -c src/ui.cpp -o ui.o

test_cpu: tests/test_cpu.cpp src/cpu.cpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude tests/test_cpu.cpp src/cpu.cpp -o tests/test_cpu

test_memory: tests/test_memory.cpp src/memory.cpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude tests/test_memory.cpp src/memory.cpp -o tests/test_memory

test_disk: tests/test_disk.cpp src/disk.cpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude tests/test_disk.cpp src/disk.cpp -o tests/test_disk

test_system: tests/test_system.cpp src/system.cpp
	g++ -std=c++17 -Wall -Wextra -O2 -Iinclude tests/test_system.cpp src/system.cpp -o tests/test_system

check: monitor test_cpu test_memory test_disk test_system
	./tests/test_cpu
	./tests/test_memory
	./tests/test_disk
	./tests/test_system

clean:
	rm -f *.o *.d monitor tests/test_cpu tests/test_memory tests/test_disk tests/test_system
	
-include *.d