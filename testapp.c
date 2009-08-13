/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#undef NDEBUG
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <netinet/in.h>

#include "config.h"
#include "cache.h"
#include "util.h"

enum test_return { TEST_SKIP, TEST_PASS, TEST_FAIL };

static enum test_return cache_create_test(void)
{
    cache_t *cache = cache_create("test", sizeof(uint32_t), sizeof(char*),
                                  NULL, NULL);
    assert(cache != NULL);
    cache_destroy(cache);
    return TEST_PASS;
}

const uint64_t constructor_pattern = 0xdeadcafebabebeef;

static int cache_constructor(void *buffer, void *notused1, int notused2) {
    uint64_t *ptr = buffer;
    *ptr = constructor_pattern;
    return 0;
}

static enum test_return cache_constructor_test(void)
{
    cache_t *cache = cache_create("test", sizeof(uint64_t), sizeof(uint64_t),
                                  cache_constructor, NULL);
    assert(cache != NULL);
    uint64_t *ptr = cache_alloc(cache);
    uint64_t pattern = *ptr;
    cache_free(cache, ptr);
    cache_destroy(cache);
    return (pattern == constructor_pattern) ? TEST_PASS : TEST_FAIL;
}

static int cache_fail_constructor(void *buffer, void *notused1, int notused2) {
    return 1;
}

static enum test_return cache_fail_constructor_test(void)
{
    enum test_return ret = TEST_PASS;

    cache_t *cache = cache_create("test", sizeof(uint64_t), sizeof(uint64_t),
                                  cache_fail_constructor, NULL);
    assert(cache != NULL);
    uint64_t *ptr = cache_alloc(cache);
    if (ptr != NULL) {
        ret = TEST_FAIL;
    }
    cache_destroy(cache);
    return ret;
}

static void *destruct_data = 0;

static void cache_destructor(void *buffer, void *notused) {
    destruct_data = buffer;
}

static enum test_return cache_destructor_test(void)
{
    cache_t *cache = cache_create("test", sizeof(uint32_t), sizeof(char*),
                                  NULL, cache_destructor);
    assert(cache != NULL);
    char *ptr = cache_alloc(cache);
    cache_free(cache, ptr);
    cache_destroy(cache);

    return (ptr == destruct_data) ? TEST_PASS : TEST_FAIL;
}

static enum test_return cache_reuse_test(void)
{
    int ii;
    cache_t *cache = cache_create("test", sizeof(uint32_t), sizeof(char*),
                                  NULL, NULL);
    char *ptr = cache_alloc(cache);
    cache_free(cache, ptr);
    for (ii = 0; ii < 100; ++ii) {
        char *p = cache_alloc(cache);
        assert(p == ptr);
        cache_free(cache, ptr);
    }
    cache_destroy(cache);
    return TEST_PASS;
}

static enum test_return cache_redzone_test(void)
{
#ifndef HAVE_UMEM_H
    cache_t *cache = cache_create("test", sizeof(uint32_t), sizeof(char*),
                                  NULL, NULL);

    /* Ignore SIGABORT */
    struct sigaction old_action;
    struct sigaction action = { .sa_handler = SIG_IGN, .sa_flags = 0};
    sigemptyset(&action.sa_mask);
    sigaction(SIGABRT, &action, &old_action);

    /* check memory debug.. */
    char *p = cache_alloc(cache);
    char old = *(p - 1);
    *(p - 1) = 0;
    cache_free(cache, p);
    assert(cache_error == -1);
    *(p - 1) = old;

    p[sizeof(uint32_t)] = 0;
    cache_free(cache, p);
    assert(cache_error == 1);

    /* restore signal handler */
    sigaction(SIGABRT, &old_action, NULL);

    cache_destroy(cache);

    return TEST_PASS;
#else
    return TEST_SKIP;
#endif
}

static enum test_return test_safe_strtoul(void) {
    uint32_t val;
    assert(safe_strtoul("123", &val));
    assert(val == 123);
    assert(safe_strtoul("+123", &val));
    assert(val == 123);
    assert(!safe_strtoul("", &val));  // empty
    assert(!safe_strtoul("123BOGUS", &val));  // non-numeric
    /* Not sure what it does, but this works with ICC :/
       assert(!safe_strtoul("92837498237498237498029383", &val)); // out of range
    */

    // extremes:
    assert(safe_strtoul("4294967295", &val)); // 2**32 - 1
    assert(val == 4294967295L);
    /* This actually works on 64-bit ubuntu
       assert(!safe_strtoul("4294967296", &val)); // 2**32
    */
    assert(!safe_strtoul("-1", &val));  // negative
    return TEST_PASS;
}


static enum test_return test_safe_strtoull(void) {
    uint64_t val;
    assert(safe_strtoull("123", &val));
    assert(val == 123);
    assert(safe_strtoull("+123", &val));
    assert(val == 123);
    assert(!safe_strtoull("", &val));  // empty
    assert(!safe_strtoull("123BOGUS", &val));  // non-numeric
    assert(!safe_strtoull("92837498237498237498029383", &val)); // out of range

    // extremes:
    assert(safe_strtoull("18446744073709551615", &val)); // 2**64 - 1
    assert(val == 18446744073709551615ULL);
    assert(!safe_strtoull("18446744073709551616", &val)); // 2**64
    assert(!safe_strtoull("-1", &val));  // negative
    return TEST_PASS;
}

static enum test_return test_safe_strtoll(void) {
    int64_t val;
    assert(safe_strtoll("123", &val));
    assert(val == 123);
    assert(safe_strtoll("+123", &val));
    assert(val == 123);
    assert(safe_strtoll("-123", &val));
    assert(val == -123);
    assert(!safe_strtoll("", &val));  // empty
    assert(!safe_strtoll("123BOGUS", &val));  // non-numeric
    assert(!safe_strtoll("92837498237498237498029383", &val)); // out of range

    // extremes:
    assert(!safe_strtoll("18446744073709551615", &val)); // 2**64 - 1
    assert(safe_strtoll("9223372036854775807", &val)); // 2**63 - 1
    assert(val == 9223372036854775807LL);
    /*
      assert(safe_strtoll("-9223372036854775808", &val)); // -2**63
      assert(val == -9223372036854775808LL);
    */
    assert(!safe_strtoll("-9223372036854775809", &val)); // -2**63 - 1

    // We'll allow space to terminate the string.  And leading space.
    assert(safe_strtoll(" 123 foo", &val));
    assert(val == 123);
    return TEST_PASS;
}

static enum test_return test_safe_strtol(void) {
    int32_t val;
    assert(safe_strtol("123", &val));
    assert(val == 123);
    assert(safe_strtol("+123", &val));
    assert(val == 123);
    assert(safe_strtol("-123", &val));
    assert(val == -123);
    assert(!safe_strtol("", &val));  // empty
    assert(!safe_strtol("123BOGUS", &val));  // non-numeric
    assert(!safe_strtol("92837498237498237498029383", &val)); // out of range

    // extremes:
    /* This actually works on 64-bit ubuntu
       assert(!safe_strtol("2147483648", &val)); // (expt 2.0 31.0)
    */
    assert(safe_strtol("2147483647", &val)); // (- (expt 2.0 31) 1)
    assert(val == 2147483647L);
    /* This actually works on 64-bit ubuntu
       assert(!safe_strtol("-2147483649", &val)); // (- (expt -2.0 31) 1)
    */

    // We'll allow space to terminate the string.  And leading space.
    assert(safe_strtol(" 123 foo", &val));
    assert(val == 123);
    return TEST_PASS;
}

/**
 * Function to start the server and let it listen on a random port
 *
 * @param port_out where to store the TCP port number the server is
 *                 listening on
 * @param daemon set to true if you want to run the memcached server
 *               as a daemon process
 * @return the pid of the memcached server
 */
static pid_t start_server(in_port_t *port_out, bool daemon) {
    char environment[80];
    snprintf(environment, sizeof(environment),
             "MEMCACHED_PORT_FILENAME=/tmp/ports.%u", getpid());
    char *filename= environment + strlen("MEMCACHED_PORT_FILENAME=");
    char pid_file[80];
    snprintf(pid_file, sizeof(pid_file), "/tmp/pid.%u", getpid());

    remove(filename);
    remove(pid_file);

    pid_t pid = fork();
    assert(pid != -1);

    if (pid == 0) {
        /* Child */
        char *argv[10];
        int arg = 0;
        putenv(environment);
        argv[arg++] = "./memcached-debug";
        argv[arg++] = "-p";
        argv[arg++] = "-1";
        argv[arg++] = "-U";
        argv[arg++] = "0";
        if (daemon) {
            argv[arg++] = "-d";
            argv[arg++] = "-P";
            argv[arg++] = pid_file;
        }
        argv[arg++] = NULL;
        assert(execv("./memcached-debug", argv) != -1);
    }

    /* Yeah just let us "busy-wait" for the file to be created ;-) */
    while (access(filename, F_OK) == -1) {
        usleep(10);
    }

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open the file containing port numbers: %s\n",
                strerror(errno));
        assert(false);
    }

    *port_out = (in_port_t)-1;
    char buffer[80];
    while ((fgets(buffer, sizeof(buffer), fp)) != NULL) {
        if (strncmp(buffer, "TCP INET: ", 10) == 0) {
            int32_t val;
            assert(safe_strtol(buffer + 10, &val));
            *port_out = (in_port_t)val;
        }
    }
    fclose(fp);
    assert(remove(filename) == 0);

    if (daemon) {
        /* loop and wait for the pid file.. There is a potential race
         * condition that the server just created the file but isn't
         * finished writing the content, but I'll take the chance....
         */
        while (access(pid_file, F_OK) == -1) {
            usleep(10);
        }

        fp = fopen(pid_file, "r");
        if (fp == NULL) {
            fprintf(stderr, "Failed to open pid file: %s\n",
                    strerror(errno));
            assert(false);
        }
        assert(fgets(buffer, sizeof(buffer), fp) != NULL);
        fclose(fp);

        int32_t val;
        assert(safe_strtol(buffer, &val));
        pid = (pid_t)val;
    }

    return pid;
}

static enum test_return test_issue_44(void) {
    in_port_t port;
    pid_t pid = start_server(&port, true);
    assert(kill(pid, SIGHUP) == 0);
    sleep(1);
    assert(kill(pid, SIGTERM) == 0);

    return TEST_PASS;
}

typedef enum test_return (*TEST_FUNC)(void);
struct testcase {
    const char *description;
    TEST_FUNC function;
};

struct testcase testcases[] = {
    { "cache_create", cache_create_test },
    { "cache_constructor", cache_constructor_test },
    { "cache_constructor_fail", cache_fail_constructor_test },
    { "cache_destructor", cache_destructor_test },
    { "cache_reuse", cache_reuse_test },
    { "cache_redzone", cache_redzone_test },
    { "strtol", test_safe_strtol },
    { "strtoll", test_safe_strtoll },
    { "strtoul", test_safe_strtoul },
    { "strtoull", test_safe_strtoull },
    { "issue_44", test_issue_44 },
    { NULL, NULL }
};

int main(int argc, char **argv)
{
    int exitcode = 0;
    int ii;
    const int paddingsz = 60;
    char padding[paddingsz + 1];
    memset(padding, ' ', paddingsz);

    for (ii = 0; testcases[ii].description != NULL; ++ii) {
        int len = strlen(testcases[ii].description);
        if (len > paddingsz) {
            len = paddingsz;
        }
        padding[paddingsz - len] = '\0';
        fprintf(stdout, "%s:%s", testcases[ii].description, padding);
        padding[paddingsz - len] = ' ';
        fflush(stdout);
        enum test_return ret = testcases[ii].function();
        if (ret == TEST_SKIP) {
            fprintf(stdout, "[skipped]\n");
        } else if (ret == TEST_PASS) {
            fprintf(stdout, "[ok]\n");
        } else {
            fprintf(stdout, "[failed]\n");
            exitcode = 1;
        }
        fflush(stdout);
    }

    return exitcode;
}