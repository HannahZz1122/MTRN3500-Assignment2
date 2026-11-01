#include "TMM.h"

error_state ThreadManagement::setupSharedMemory() {
	return error_state::SUCCESS;
}

error_state ThreadManagement::processSharedMemory() {
	return error_state::SUCCESS;
}

void ThreadManagement::shutdownModules() {}

bool ThreadManagement::getShutdownFlag() {
	return true;
}

void ThreadManagement::threadFunction() {}