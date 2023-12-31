/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/cleanse_wrapper.h"
#include "crypto/elliptic_curve_key.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"
#include "openssl/rand.h"
#include "test_util.h"
#include "util.h"

extern "C" {
#include "sha256.h"
}

#include <array>
#include <memory>

test_static enum ec_error_list test_rand(void)
{
	constexpr uint8_t zero[256] = { 0 };
	uint8_t buf1[256];
	uint8_t buf2[256];

	RAND_bytes(buf1, sizeof(buf1));
	RAND_bytes(buf2, sizeof(buf2));

	TEST_ASSERT_ARRAY_NE(buf1, zero, sizeof(zero));
	TEST_ASSERT_ARRAY_NE(buf2, zero, sizeof(zero));
	TEST_ASSERT_ARRAY_NE(buf1, buf2, sizeof(buf1));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_ecc_keygen(void)
{
	bssl::UniquePtr<EC_KEY> key1 = generate_elliptic_curve_key();

	TEST_NE(key1.get(), nullptr, "%p");

	/* The generated key should be valid.*/
	TEST_EQ(EC_KEY_check_key(key1.get()), 1, "%d");

	bssl::UniquePtr<EC_KEY> key2 = generate_elliptic_curve_key();

	TEST_NE(key2.get(), nullptr, "%p");

	/* The generated key should be valid. */
	TEST_EQ(EC_KEY_check_key(key2.get()), 1, "%d");

	const BIGNUM *priv1 = EC_KEY_get0_private_key(key1.get());
	const BIGNUM *priv2 = EC_KEY_get0_private_key(key2.get());

	/* The generated keys should not be the same. */
	TEST_NE(BN_cmp(priv1, priv2), 0, "%d");

	/* The generated keys should not be zero. */
	TEST_EQ(BN_is_zero(priv1), 0, "%d");
	TEST_EQ(BN_is_zero(priv2), 0, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_cleanse_wrapper_std_array(void)
{
	using TypeToCheck = std::array<uint8_t, 6>;
	using Wrapped = CleanseWrapper<TypeToCheck>;

	/* Preserve a space for it. */
	uint8_t *buffer = static_cast<uint8_t *>(malloc(sizeof(Wrapped)));
	Wrapped *data = reinterpret_cast<Wrapped *>(buffer);

	/* Call the constructor. */
	new (data) Wrapped({ 1, 1, 1, 1, 1, 1 });

	TEST_ASSERT_MEMSET(data->data(), 1, data->size());

	/* Call the destructor. */
	data->~Wrapped();

	TEST_ASSERT_MEMSET(buffer, 0, sizeof(Wrapped));

	/* Free the space. */
	free(buffer);

	return EC_SUCCESS;
}

test_static enum ec_error_list test_cleanse_wrapper_sha256(void)
{
	using TypeToCheck = struct sha256_ctx;
	using Wrapped = CleanseWrapper<TypeToCheck>;

	/* Preserve a space for it. */
	uint8_t *buffer = static_cast<uint8_t *>(malloc(sizeof(Wrapped)));
	Wrapped *data = reinterpret_cast<Wrapped *>(buffer);

	/* Call the constructor. */
	new (data) Wrapped();

	std::array<uint8_t, 5> data_to_sha = { 1, 2, 3, 4, 5 };
	SHA256_init(data);
	SHA256_update(data, data_to_sha.data(), data_to_sha.size());
	uint8_t *result = SHA256_final(data);

	std::array<uint8_t, 32> expected_result = {
		0X74, 0XF8, 0X1F, 0XE1, 0X67, 0XD9, 0X9B, 0X4C,
		0XB4, 0X1D, 0X6D, 0X0C, 0XCD, 0XA8, 0X22, 0X78,
		0XCA, 0XEE, 0X9F, 0X3E, 0X2F, 0X25, 0XD5, 0XE5,
		0XA3, 0X93, 0X6F, 0XF3, 0XDC, 0XEC, 0X60, 0XD0,
	};

	TEST_ASSERT_ARRAY_EQ(result, expected_result, expected_result.size());

	/* Call the destructor. */
	data->~Wrapped();

	TEST_ASSERT_MEMSET(buffer, 0, sizeof(Wrapped));

	/* Free the space. */
	free(buffer);

	return EC_SUCCESS;
}

test_static enum ec_error_list test_cleanse_wrapper_custom_struct(void)
{
	struct TestingStruct {
		bool used;
		std::array<uint32_t, 4> data;
	};

	using TypeToCheck = TestingStruct;
	using Wrapped = CleanseWrapper<TypeToCheck>;

	/* Preserve a space for it. */
	uint8_t *buffer = static_cast<uint8_t *>(malloc(sizeof(Wrapped)));
	Wrapped *data = reinterpret_cast<Wrapped *>(buffer);

	/* Call the constructor. */
	new (data) Wrapped({
		.used = true,
		.data = { 0x7fffffffu, 0x12345678u, 0x0u, 0x42u },
	});

	TEST_EQ(data->used, true, "%d");
	TEST_EQ(data->data[0], 0x7fffffffu, "%d");
	TEST_EQ(data->data[1], 0x12345678u, "%d");
	TEST_EQ(data->data[2], 0x0u, "%d");
	TEST_EQ(data->data[3], 0x42u, "%d");

	/* Call the destructor. */
	data->~Wrapped();

	TEST_ASSERT_MEMSET(buffer, 0, sizeof(Wrapped));

	/* Free the space. */
	free(buffer);

	return EC_SUCCESS;
}

test_static enum ec_error_list test_cleanse_wrapper_normal_usage(void)
{
	struct TestingStruct {
		bool used;
		std::array<uint32_t, 4> data;
	};

	CleanseWrapper<std::array<uint8_t, 6> > array({ 1, 1, 1, 1, 1, 1 });

	TEST_ASSERT_MEMSET(array.data(), 1, array.size());

	CleanseWrapper<TestingStruct> data({
		.used = true,
		.data = { 0x7fffffffu, 0x12345678u, 0x0u, 0x42u },
	});

	TEST_EQ(data.used, true, "%d");
	TEST_EQ(data.data[0], 0x7fffffffu, "%d");
	TEST_EQ(data.data[1], 0x12345678u, "%d");
	TEST_EQ(data.data[2], 0x0u, "%d");
	TEST_EQ(data.data[3], 0x42u, "%d");

	CleanseWrapper<struct sha256_ctx> ctx;

	std::array<uint8_t, 5> data_to_sha = { 1, 2, 3, 4, 5 };
	SHA256_init(&ctx);
	SHA256_update(&ctx, data_to_sha.data(), data_to_sha.size());
	uint8_t *result = SHA256_final(&ctx);

	std::array<uint8_t, 32> expected_result = {
		0X74, 0XF8, 0X1F, 0XE1, 0X67, 0XD9, 0X9B, 0X4C,
		0XB4, 0X1D, 0X6D, 0X0C, 0XCD, 0XA8, 0X22, 0X78,
		0XCA, 0XEE, 0X9F, 0X3E, 0X2F, 0X25, 0XD5, 0XE5,
		0XA3, 0X93, 0X6F, 0XF3, 0XDC, 0XEC, 0X60, 0XD0,
	};

	TEST_ASSERT_ARRAY_EQ(result, expected_result, expected_result.size());

	/* There is no way to check the context is cleared without undefined
	 * behavior. */

	return EC_SUCCESS;
}

extern "C" void run_test(int argc, const char **argv)
{
	RUN_TEST(test_rand);
	RUN_TEST(test_ecc_keygen);
	RUN_TEST(test_cleanse_wrapper_std_array);
	RUN_TEST(test_cleanse_wrapper_sha256);
	RUN_TEST(test_cleanse_wrapper_custom_struct);
	RUN_TEST(test_cleanse_wrapper_normal_usage);
	test_print_result();
}
