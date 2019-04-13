#include <stdio.h>
#include <comp421/hardware.h>

int main(int argc, char *argv[]) {
	TracePrintf(0, "Running Idle...\n");
	while(1) {
		Pause();
	}
}