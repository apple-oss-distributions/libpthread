#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <TargetConditionals.h>

#include <pthread/pthread_spis.h>

#include <sys/sysctl.h>

#include "darwintest_defaults.h"
#include <darwintest_multiprocess.h>

struct context {
	pthread_mutex_t mutex;
	long value;
	long count;
};

static void *test_thread(void *ptr) {
	int res;
	long old;
	struct context *context = ptr;

	int i = 0;
	char *str;

	do {
		bool try = i++ & 1;

		if (!try){
			str = "pthread_mutex_lock";
			res = pthread_mutex_lock(&context->mutex);
		} else {
			str = "pthread_mutex_trylock";
			res = pthread_mutex_trylock(&context->mutex);
		}
		if (res != 0) {
			if (try && res == EBUSY) {
				continue;
			}
			T_ASSERT_POSIX_ZERO(res, "[%ld] %s", context->count, str);
		}
		
		old = __sync_fetch_and_or(&context->value, 1);
		if ((old & 1) != 0) {
			T_FAIL("[%ld] OR %lx\n", context->count, old);
		}

		old = __sync_fetch_and_and(&context->value, 0);
		if ((old & 1) == 0) {
			T_FAIL("[%ld] AND %lx\n", context->count, old);
		}
	
		res = pthread_mutex_unlock(&context->mutex);
		if (res) {
			T_ASSERT_POSIX_ZERO(res, "[%ld] pthread_mutex_lock", context->count);
		}
	} while (__sync_fetch_and_sub(&context->count, 1) > 0);

	T_PASS("thread completed successfully");

	return NULL;
}

T_DECL(mutex, "pthread_mutex",
#if TARGET_OS_VISION
	// rdar://89838865
	T_META_BOOTARGS_SET("sched_preemption_disable_debug_mode=0"),
#endif
	T_META_ALL_VALID_ARCHS(YES))
{
	struct context context = {
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.value = 0,
		.count = 1000000,
	};
	int i;
	int res;
	int threads = 8;
	pthread_t p[threads];
	for (i = 0; i < threads; ++i) {
		res = pthread_create(&p[i], NULL, test_thread, &context);
		T_ASSERT_POSIX_ZERO(res, "pthread_create()");
	}
	for (i = 0; i < threads; ++i) {
		res = pthread_join(p[i], NULL);
		T_ASSERT_POSIX_ZERO(res, "pthread_join()");
	}
}

static void
check_process_default_mutex_policy(int expected_policy)
{
	pthread_mutexattr_t mattr;
	T_EXPECT_POSIX_ZERO(pthread_mutexattr_init(&mattr), "pthread_mutexattr_init()");

	int policy;
	T_EXPECT_POSIX_ZERO(pthread_mutexattr_getpolicy_np(&mattr, &policy),
			"pthread_mutexattr_getpolicy_np()");
	T_LOG("policy was %d", policy);
	T_EXPECT_EQ(policy, expected_policy, "Saw the expected default policy");

	T_EXPECT_POSIX_ZERO(pthread_mutexattr_destroy(&mattr), "pthread_mutexattr_destroy()");
}

T_DECL(mutex_default_policy,
		"Tests that the default mutex policy is firstfit")
{
	check_process_default_mutex_policy(_PTHREAD_MUTEX_POLICY_FIRSTFIT);
}

T_DECL(mutex_default_policy_sysctl,
		"Tests that setting the policy sysctl changes the default policy")
{
	int fairshare_default = _PTHREAD_MUTEX_POLICY_FAIRSHARE;
	T_EXPECT_POSIX_ZERO(
			sysctlbyname("kern.pthread_mutex_default_policy", NULL, NULL, &fairshare_default, sizeof(fairshare_default)),
			"Changed the default policy sysctl to fairshare");

	dt_helper_t helper = dt_child_helper("mutex_default_policy_sysctl_helper");
	dt_run_helpers(&helper, 1, 5);
}

T_HELPER_DECL(mutex_default_policy_sysctl_helper, "sysctl helper")
{
	check_process_default_mutex_policy(_PTHREAD_MUTEX_POLICY_FAIRSHARE);

	int default_default = _PTHREAD_MUTEX_POLICY_FIRSTFIT;
	T_EXPECT_POSIX_ZERO(
			sysctlbyname("kern.pthread_mutex_default_policy", NULL, NULL, &default_default, sizeof(default_default)),
			"Restored the default policy to firstfit");

	T_END;
}

T_DECL(mutex_default_policy_envvar,
		"Tests that setting the policy environment variable changes the default policy",
		T_META_ENVVAR("PTHREAD_MUTEX_DEFAULT_POLICY=1"))
{
	check_process_default_mutex_policy(_PTHREAD_MUTEX_POLICY_FAIRSHARE);
}

static void *
mutex_as_semaphore_signaller(void *arg)
{
	pthread_mutex_t *mtx = arg;
	int rc = pthread_mutex_unlock(mtx);
	T_ASSERT_POSIX_ZERO(rc, "unlock");

	return NULL;
}

T_DECL(mutex_as_semaphore_lock_owned, "Recursively lock a normal mutex to use as a semaphore")
{
	pthread_t signaller;
	pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
	int rc = pthread_mutex_lock(&mtx);
	T_ASSERT_POSIX_ZERO(rc, "lock");

	rc = pthread_create(&signaller, NULL, mutex_as_semaphore_signaller, &mtx);
	T_ASSERT_POSIX_ZERO(rc, "create");

	rc = pthread_mutex_lock(&mtx);
	T_ASSERT_POSIX_ZERO(rc, "recursive lock");

	rc = pthread_join(signaller, NULL);
	T_ASSERT_POSIX_ZERO(rc, "join");

	T_END;
}
