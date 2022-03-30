/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <daos/tests_lib.h>
#include <gurt/debug.h>
#include <ddb_common.h>
#include <ddb_parse.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

#define assert_parsed_words2(str, count, ...) \
	__assert_parsed_words2(str, count, (char *[])__VA_ARGS__)
static void
__assert_parsed_words2(const char *str, int count, char **expected_words)
{
	struct argv_parsed	parse_args;
	int			i;

	assert_success(ddb_str2argv_create(str, &parse_args));
	assert_int_equal(count, parse_args.ap_argc);

	for (i = 0; i < parse_args.ap_argc; i++)
		assert_string_equal(parse_args.ap_argv[i], expected_words[i]);

	ddb_str2argv_free(&parse_args);
}

static void
assert_parsed_fail(const char *str)
{
	struct argv_parsed parse_args;

	assert_rc_equal(-DER_INVAL, ddb_str2argv_create(str, &parse_args));
	ddb_str2argv_free(&parse_args);
}

/*
 * -----------------------------------------------
 * Test implementations
 * -----------------------------------------------
 */

static void
test_string_to_argv(void **state)
{
	assert_parsed_words2("one", 1, { "one" });
	assert_parsed_words2("one two", 2, {"one", "two"});
	assert_parsed_words2("one two three four five", 5, {"one", "two", "three", "four", "five"});
	assert_parsed_words2("one 'two two two'", 2, {"one", "two two two"});
	assert_parsed_words2("one 'two two two' three", 3, {"one", "two two two", "three"});
	assert_parsed_words2("one \"two two two\" three", 3, {"one", "two two two", "three"});

	assert_parsed_fail("one>");
	assert_parsed_fail("one<");
	assert_parsed_fail("'one");
	assert_parsed_fail(" \"one");
	assert_parsed_fail("one \"two");
}

#define assert_invalid_program_args(argc, ...) \
	assert_rc_equal(-DER_INVAL, _assert_invalid_program_args(argc, ((char*[])__VA_ARGS__)))
static int
_assert_invalid_program_args(uint32_t argc, char **argv)
{
	struct program_args	pa;

	return ddb_parse_program_args(argc, argv, &pa);
}

#define assert_program_args(expected_program_args, argc, ...) \
	assert_success(_assert_program_args(&expected_program_args, argc, ((char*[])__VA_ARGS__)))
static int
_assert_program_args(struct program_args *expected_pa, uint32_t argc, char **argv)
{
	struct program_args pa = {0};
	int rc;

	rc = ddb_parse_program_args(argc, argv, &pa);
	if (rc != 0)
		return rc;

	if (strcmp(expected_pa->pa_r_cmd_run, pa.pa_r_cmd_run) != 0) {
		print_error("ERROR: %s != %s\n", expected_pa->pa_r_cmd_run, pa.pa_r_cmd_run);
		return -DER_INVAL;
	}

	if (strcmp(expected_pa->pa_cmd_file, pa.pa_cmd_file) != 0) {
		print_error("ERROR: %s != %s\n", expected_pa->pa_cmd_file, pa.pa_cmd_file);
		return -DER_INVAL;
	}

	return 0;
}

static void
test_parse_args(void **state)
{
	struct program_args pa = {0};

	assert_invalid_program_args(2, {"", "-z"});
	assert_invalid_program_args(3, {"", "command1", "command2"});

	strcpy(pa.pa_r_cmd_run, "command");
	assert_program_args(pa, 3, {"", "-R", "command"});
	strcpy(pa.pa_r_cmd_run, "");

	strcpy(pa.pa_cmd_file, "path");
	assert_program_args(pa, 3, {"", "-f", "path"});
}

#define assert_vtp_eq(a, b) \
do { \
	assert_uuid_equal(a.vtp_path.vtp_cont, b.vtp_path.vtp_cont); \
	assert_int_equal(a.vtp_cont_idx, b.vtp_cont_idx); \
	assert_int_equal(a.vtp_oid_idx, b.vtp_oid_idx); \
	assert_int_equal(a.vtp_dkey_idx, b.vtp_dkey_idx); \
	assert_int_equal(a.vtp_akey_idx, b.vtp_akey_idx); \
	assert_int_equal(a.vtp_recx_idx, b.vtp_recx_idx); \
	assert_int_equal(a.vtp_path.vtp_oid.id_pub.hi, b.vtp_path.vtp_oid.id_pub.hi); \
	assert_int_equal(a.vtp_path.vtp_oid.id_pub.lo, b.vtp_path.vtp_oid.id_pub.lo); \
	assert_int_equal(a.vtp_path.vtp_dkey.iov_len, b.vtp_path.vtp_dkey.iov_len); \
	if (a.vtp_path.vtp_dkey.iov_len > 0) \
		assert_string_equal(a.vtp_path.vtp_dkey.iov_buf, b.vtp_path.vtp_dkey.iov_buf); \
	assert_int_equal(a.vtp_path.vtp_akey.iov_len, b.vtp_path.vtp_akey.iov_len); \
	if (a.vtp_path.vtp_akey.iov_len > 0) \
		assert_string_equal(a.vtp_path.vtp_akey.iov_buf, b.vtp_path.vtp_akey.iov_buf); \
} while (0)

#define assert_invalid_path(path) \
do { \
	struct dv_tree_path_builder __vt = {0}; \
	assert_rc_equal(-DER_INVAL, ddb_parse_vos_tree_path(path, &__vt)); \
} while (0)

#define assert_path(path, expected) \
do { \
	struct dv_tree_path_builder __vt = {0}; \
	assert_success(ddb_parse_vos_tree_path(path, &__vt)); \
	assert_vtp_eq(expected, __vt); \
} while (0)


/** easily setup an iov and allocate */
static void
iov_alloc(d_iov_t *iov, size_t len)
{
	D_ALLOC(iov->iov_buf, len);
	iov->iov_buf_len = iov->iov_len = len;
}

static void
iov_alloc_str(d_iov_t *iov, const char *str)
{
	iov_alloc(iov, strlen(str));
	strcpy(iov->iov_buf, str);
}

static void
test_vos_path_parse(void **state)
{
	struct dv_tree_path_builder expected_vt = {0};
	daos_obj_id_t oid;

	ddb_vos_tree_path_setup(&expected_vt);

	/* empty paths are valid */
	assert_path("", expected_vt);
	assert_path(NULL, expected_vt);

	/* first part must be a valid uuid */
	assert_invalid_path("12345678");

	uuid_parse("12345678-1234-1234-1234-123456789012", expected_vt.vtp_path.vtp_cont);
	oid.lo = 1234;
	oid.hi = 4321;

	/* handle just container */
	assert_path("12345678-1234-1234-1234-123456789012", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012", expected_vt);
	assert_path("12345678-1234-1234-1234-123456789012/", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012/", expected_vt);

	/* handle container and object id */
	assert_invalid_path("/12345678-1234-1234-1234-123456789012/4321.");
	expected_vt.vtp_path.vtp_oid.id_pub.lo = 1234;
	expected_vt.vtp_path.vtp_oid.id_pub.hi = 4321;

	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234", expected_vt);

	/* handle dkey */
	iov_alloc_str(&expected_vt.vtp_path.vtp_dkey, "dkey");
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/dkey", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/dkey/", expected_vt);

	iov_alloc_str(&expected_vt.vtp_path.vtp_akey, "akey");
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/dkey/akey", expected_vt);
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/dkey/akey/", expected_vt);

	expected_vt.vtp_path.vtp_recx.rx_idx = 1;
	expected_vt.vtp_path.vtp_recx.rx_nr = 5;
	assert_path("/12345678-1234-1234-1234-123456789012/4321.1234/dkey/akey/{1-6}", expected_vt);
}

static void
test_parse_idx(void **state)
{
	struct dv_tree_path_builder expected_vt = {0};

	ddb_vos_tree_path_setup(&expected_vt);

	expected_vt.vtp_cont_idx = 1;
	assert_path("[1]", expected_vt);

	expected_vt.vtp_cont_idx = 11;
	assert_path("[11]", expected_vt);

	expected_vt.vtp_cont_idx = 1234;
	assert_path("[1234]", expected_vt);

	expected_vt.vtp_cont_idx = 1;
	expected_vt.vtp_oid_idx = 2;
	expected_vt.vtp_dkey_idx = 3;
	expected_vt.vtp_akey_idx = 4;

	expected_vt.vtp_recx_idx = 5;
	assert_path("[1]/[2]/[3]/[4]/[5]", expected_vt);
}

static void
test_has_parts(void **state)
{
	struct dv_tree_path vtp = {0};

	assert_false(dv_has_cont(&vtp));
	uuid_copy(vtp.vtp_cont, g_uuids[0]);
	assert_true(dv_has_cont(&vtp));

	assert_false(dv_has_obj(&vtp));
	vtp.vtp_oid = g_oids[0];
	assert_true(dv_has_obj(&vtp));

	assert_false(dv_has_dkey(&vtp));
	vtp.vtp_dkey = g_dkeys[0];
	assert_true(dv_has_dkey(&vtp));

	assert_false(dv_has_akey(&vtp));
	vtp.vtp_akey = g_akeys[0];
	assert_true(dv_has_akey(&vtp));
}

#define TEST(dsc, test) { dsc, test, NULL, NULL }

static const struct CMUnitTest tests[] = {
	TEST("01: string to argv", test_string_to_argv),
	TEST("03: parse program arguments", test_parse_args),
	TEST("04: parse a vos path", test_vos_path_parse),
	TEST("05: parse a vos path with indexes", test_parse_idx),
	TEST("06: check to see if the path has parts", test_has_parts)
};

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
int
ddb_parse_tests_run()
{
	return cmocka_run_group_tests_name("DAOS Checksum Tests", tests,
					   ddb_suit_setup, ddb_suit_teardown);
}
