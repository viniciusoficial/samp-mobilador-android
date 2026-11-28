#pragma once

#include <sched.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

class ThreadOptimizer {
public:
	ThreadOptimizer() = default;
	~ThreadOptimizer() = default;

	// Apply CPU affinity once on thread launch using proper cpu_set_t sizing
	// (avoids arm64 EINVAL from 4-byte uint32_t mask and eliminates detached-thread UAF).
	void pushThread(pid_t tid)
	{
		cpu_set_t set;
		CPU_ZERO(&set);
		long ncpu = sysconf(_SC_NPROCESSORS_CONF);
		if (ncpu <= 0 || ncpu > CPU_SETSIZE) ncpu = 8;
		for (long i = 0; i < ncpu; ++i) {
			CPU_SET(i, &set);
		}
		sched_setaffinity(tid, sizeof(set), &set);
	}
};
