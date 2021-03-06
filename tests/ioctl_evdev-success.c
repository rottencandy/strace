/*
 * Copyright (c) 2018 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"

#ifdef HAVE_LINUX_INPUT_H

# include <assert.h>
# include <inttypes.h>
# include <stdio.h>
# include <stdlib.h>
# include <sys/ioctl.h>
# include <linux/input.h>
# include "print_fields.h"

# define NUM_WORDS 4

static const char *errstr;

struct evdev_check {
	unsigned long cmd;
	const char *cmd_str;
	const void *arg_ptr;
	void (*print_arg)(long rc, const void *ptr, const void *arg);
};

static long
invoke_test_syscall(unsigned long cmd, const void *p)
{
	long rc = ioctl(-1, cmd, p);
	errstr = sprintrc(rc);
	static char inj_errstr[4096];

	snprintf(inj_errstr, sizeof(inj_errstr), "%s (INJECTED)", errstr);
	errstr = inj_errstr;
	return rc;
}

static void
test_evdev(struct evdev_check *check, const void *arg)
{
	long rc = invoke_test_syscall(check->cmd, check->arg_ptr);
	printf("ioctl(-1, %s, ", check->cmd_str);
	if (check->print_arg)
		check->print_arg(rc, check->arg_ptr, arg);
	else
		printf("%p", check->arg_ptr);
	printf(") = %s\n", errstr);
}

static void
print_input_absinfo(long rc, const void *ptr, const void *arg)
{
	const struct input_absinfo *absinfo = ptr;

	if (rc < 0) {
		printf("%p", absinfo);
		return;
	}
	PRINT_FIELD_U("{", *absinfo, value);
	PRINT_FIELD_U(", ", *absinfo, minimum);
# if VERBOSE
	PRINT_FIELD_U(", ", *absinfo, maximum);
	PRINT_FIELD_U(", ", *absinfo, fuzz);
	PRINT_FIELD_U(", ", *absinfo, flat);
#  ifdef HAVE_STRUCT_INPUT_ABSINFO_RESOLUTION
	PRINT_FIELD_U(", ", *absinfo, resolution);
#  endif
# else
	printf(", ...");
# endif
	printf("}");
}

static void
print_input_id(long rc, const void *ptr, const void *arg)
{
	const struct input_id *id = ptr;

	if (rc < 0) {
		printf("%p", id);
		return;
	}
	printf("{ID_BUS=%" PRIu16
	       ", ID_VENDOR=%" PRIu16
	       ", ID_PRODUCT=%" PRIu16
	       ", ID_VERSION=%" PRIu16 "}",
	       id->bustype, id->vendor, id->product, id->version);
}

# ifdef EVIOCGMTSLOTS
static void
print_mtslots(long rc, const void *ptr, const void *arg)
{
	const int *buffer = ptr;
	const char * const * str = arg;
	int num = atoi(*(str + 1));

	if (rc < 0) {
		printf("%p", buffer);
		return;
	}

	printf("{code=%s", *str);
	printf(", values=[");
	for (unsigned int i = 1; i <= (unsigned) num; i++)
		printf("%s%s", i > 1 ? ", " : "", *(str + i + 1));
	printf("]}");
}
# endif

static void
print_getbit(long rc, const void *ptr, const void *arg)
{
	const char * const *str = arg;

	if (rc <= 0) {
		printf("%p", ptr);
		return;
	}

	printf("[");
	for (unsigned long i = 0; str[i]; i++) {
# if ! VERBOSE
		if (i >= 4) {
			printf(", ...");
			break;
		}
# endif
		if (i)
			printf(", ");
		printf("%s", str[i]);
	}
	printf("]");
}

int
main(int argc, char **argv)
{
	unsigned long num_skip;
	long inject_retval;
	bool locked = false;

	if (argc == 1)
		return 0;

	if (argc < 3)
		error_msg_and_fail("Usage: %s NUM_SKIP INJECT_RETVAL", argv[0]);

	num_skip = strtoul(argv[1], NULL, 0);
	inject_retval = strtol(argv[2], NULL, 0);

	if (inject_retval < 0)
		error_msg_and_fail("Expected non-negative INJECT_RETVAL, "
				   "but got %ld", inject_retval);

	for (unsigned int i = 0; i < num_skip; i++) {
		long rc = ioctl(-1, EVIOCGID, NULL);
		printf("ioctl(-1, EVIOCGID, NULL) = %s%s\n",
		       sprintrc(rc),
		       rc == inject_retval ? " (INJECTED)" : "");

		if (rc != inject_retval)
			continue;

		locked = true;
		break;
	}

	if (!locked)
		error_msg_and_fail("Hasn't locked on ioctl(-1"
				   ", EVIOCGID, NULL) returning %lu",
				   inject_retval);

	TAIL_ALLOC_OBJECT_CONST_PTR(struct input_id, id);
	TAIL_ALLOC_OBJECT_CONST_PTR(struct input_absinfo, absinfo);
	TAIL_ALLOC_OBJECT_CONST_PTR(int, bad_addr_slot);

# ifdef EVIOCGMTSLOTS
	int mtslots[] = { ABS_MT_SLOT, 1, 3 };
	/* we use the second element to indicate the number of values */
	/* mtslots_str[1] is "2" so the number of values is 2 */
	const char *mtslots_str[] = { "ABS_MT_SLOT", "2", "1", "3" };

	/* invalid flag */
	int invalid_mtslot[] = { -1, 1 };
	char invalid_str[4096];
	snprintf(invalid_str, sizeof(invalid_str), "%#x /* ABS_MT_??? */", invalid_mtslot[0]);
	const char *invalid_mtslot_str[] = { invalid_str, "1", "1" };
# endif

	enum { ULONG_BIT = sizeof(unsigned long) * 8 };

	/* set more than 4 bits */
	static const unsigned long ev_more[NUM_WORDS] = {
		1 << EV_ABS | 1 << EV_MSC | 1 << EV_LED | 1 << EV_SND
		| 1 << EV_PWR };
	static const char * const ev_more_str_2[] = {
		"EV_ABS", "EV_MSC", NULL };
	static const char * const ev_more_str_3[] = {
		"EV_ABS", "EV_MSC", "EV_LED", "EV_SND", "EV_PWR", NULL };

	/* set less than 4 bits */
	static const unsigned long ev_less[NUM_WORDS] = {
		1 << EV_ABS | 1 << EV_MSC | 1 << EV_LED };
	static const char * const ev_less_str_2[] = {
		"EV_ABS", "EV_MSC", NULL };
	static const char * const ev_less_str_3[] = {
		"EV_ABS", "EV_MSC", "EV_LED", NULL };

	/* set zero bit */
	static const unsigned long ev_zero[NUM_WORDS] = { 0x0 };
	static const char * const ev_zero_str[] = { " 0 ", NULL };

	/* KEY_MAX is 0x2ff which is greater than retval * 8 */
	static const unsigned long key[NUM_WORDS] = {
		1 << KEY_1 | 1 << KEY_2,
		[ KEY_F12 / ULONG_BIT ] = 1 << (KEY_F12 % ULONG_BIT) };

	static const char * const key_str_8[] = {
		"KEY_1", "KEY_2", NULL };
	static const char * const key_str_16[] = {
		"KEY_1", "KEY_2", "KEY_F12", NULL };

	assert(sizeof(ev_more) >= (unsigned long) inject_retval);
	assert(sizeof(ev_less) >= (unsigned long) inject_retval);
	assert(sizeof(ev_zero) >= (unsigned long) inject_retval);
	assert(sizeof(key) >= (unsigned long) inject_retval);

	struct {
		struct evdev_check check;
		const void *ptr;
	} a[] = {
		{ { ARG_STR(EVIOCGID), id, print_input_id }, NULL },
		{ { ARG_STR(EVIOCGABS(ABS_X)), absinfo, print_input_absinfo }, NULL },
		{ { ARG_STR(EVIOCGABS(ABS_Y)), absinfo, print_input_absinfo }, NULL },
		{ { ARG_STR(EVIOCGABS(ABS_Y)), absinfo, print_input_absinfo }, NULL },
		{ { ARG_STR(EVIOCGBIT(0, 0)), ev_more, print_getbit },
			inject_retval * 8 <= EV_LED
				? (const void *) &ev_more_str_2
				: (const void *) &ev_more_str_3 },
		{ { ARG_STR(EVIOCGBIT(0, 0)), ev_less, print_getbit },
			inject_retval * 8 <= EV_LED
				? (const void *) &ev_less_str_2
				: (const void *) &ev_less_str_3 },
		{ { ARG_STR(EVIOCGBIT(0, 0)), ev_zero, print_getbit }, &ev_zero_str },
		{ { ARG_STR(EVIOCGBIT(EV_KEY, 0)), key, print_getbit },
			inject_retval * 8 <= KEY_F12
				? (const void *) &key_str_8
				: (const void *) &key_str_16 },
# ifdef EVIOCGMTSLOTS
		{ { ARG_STR(EVIOCGMTSLOTS(12)), mtslots, print_mtslots }, &mtslots_str },
		{ { ARG_STR(EVIOCGMTSLOTS(8)), invalid_mtslot, print_mtslots }, &invalid_mtslot_str }
# endif
	};
	for (unsigned int i = 0; i < ARRAY_SIZE(a); i++) {
		test_evdev(&a[i].check, a[i].ptr);
	}

	puts("+++ exited with 0 +++");
	return 0;
}
#else

SKIP_MAIN_UNDEFINED("HAVE_LINUX_INPUT_H")

#endif
