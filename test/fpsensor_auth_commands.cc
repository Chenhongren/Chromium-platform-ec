/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "crypto/elliptic_curve_key.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "fpsensor/fpsensor_state.h"
#include "mock/fpsensor_state_mock.h"
#include "openssl/aes.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/obj_mac.h"
#include "test_util.h"
#include "util.h"

extern "C" {
#include "sha256.h"
}

#include <stdbool.h>

#include <array>

namespace
{

enum ec_error_list
check_seed_set_result(const enum ec_status rv, const uint32_t expected,
		      const struct ec_response_fp_encryption_status *resp)
{
	const uint32_t actual = resp->status & FP_ENC_STATUS_SEED_SET;

	if (rv != EC_RES_SUCCESS || expected != actual) {
		ccprintf("%s:%s(): rv = %d, seed is set: %d\n", __FILE__,
			 __func__, rv, actual);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

test_static enum ec_error_list test_set_fp_tpm_seed(void)
{
	enum ec_status rv;
	struct ec_params_fp_seed params;
	struct ec_response_fp_encryption_status resp = { 0 };

	params.struct_version = FP_TEMPLATE_FORMAT_VERSION;
	memcpy(params.seed, default_fake_tpm_seed,
	       sizeof(default_fake_tpm_seed));

	rv = test_send_host_command(EC_CMD_FP_SEED, 0, &params, sizeof(params),
				    NULL, 0);
	if (rv != EC_RES_SUCCESS) {
		ccprintf("%s:%s(): rv = %d, set seed failed\n", __FILE__,
			 __func__, rv);
		return EC_ERROR_UNKNOWN;
	}

	/* Now seed should have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				    sizeof(resp));

	return check_seed_set_result(rv, FP_ENC_STATUS_SEED_SET, &resp);
}

test_static enum ec_error_list test_fp_command_check_context_cleared(void)
{
	fp_reset_and_clear_context();
	TEST_EQ(check_context_cleared(), EC_SUCCESS, "%d");

	struct ec_params_fp_context_v1 params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 0, 1, 2, 3, 4, 5, 6, 7 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &params,
				       sizeof(params), NULL, 0),
		EC_RES_SUCCESS, "%d");
	TEST_EQ(check_context_cleared(), EC_ERROR_ACCESS_DENIED, "%d");

	fp_reset_and_clear_context();
	TEST_EQ(check_context_cleared(), EC_SUCCESS, "%d");

	templ_valid++;
	TEST_EQ(check_context_cleared(), EC_ERROR_ACCESS_DENIED, "%d");

	fp_reset_and_clear_context();
	TEST_EQ(check_context_cleared(), EC_SUCCESS, "%d");

	templ_dirty |= BIT(0);
	TEST_EQ(check_context_cleared(), EC_ERROR_ACCESS_DENIED, "%d");

	fp_reset_and_clear_context();
	TEST_EQ(check_context_cleared(), EC_SUCCESS, "%d");

	positive_match_secret_state.template_matched = 0;
	TEST_EQ(check_context_cleared(), EC_ERROR_ACCESS_DENIED, "%d");

	fp_reset_and_clear_context();
	TEST_EQ(check_context_cleared(), EC_SUCCESS, "%d");

	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(check_context_cleared(), EC_ERROR_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_pairing_key_without_seed(void)
{
	enum ec_status rv;
	struct ec_response_fp_encryption_status resp = { 0 };
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;

	/* Seed shouldn't have been set. */
	rv = test_send_host_command(EC_CMD_FP_ENC_STATUS, 0, NULL, 0, &resp,
				    sizeof(resp));

	TEST_EQ(check_seed_set_result(rv, 0, &resp), EC_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_NE(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_pairing_key_keygen(void)
{
	enum ec_status rv;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	bssl::UniquePtr<EC_KEY> pubkey =
		create_ec_key_from_pubkey(keygen_response.pubkey);

	TEST_NE(pubkey.get(), nullptr, "%p");
	TEST_EQ(EC_KEY_check_key(pubkey.get()), 1, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_establish_and_load_pairing_key(void)
{
	enum ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response;
	ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&wrap_params.encrypted_private_key.info,
	       &keygen_response.encrypted_private_key.info,
	       sizeof(keygen_response.encrypted_private_key.info));

	memcpy(wrap_params.encrypted_private_key.data,
	       keygen_response.encrypted_private_key.data,
	       sizeof(keygen_response.encrypted_private_key.data));

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_establish_pairing_key_fail(void)
{
	enum ec_status rv;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	struct ec_response_fp_establish_pairing_key_wrap wrap_response;

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* No encryption info. */
	memset(&wrap_params.encrypted_private_key.info, 0,
	       sizeof(wrap_params.encrypted_private_key.info));

	memcpy(wrap_params.encrypted_private_key.data,
	       keygen_response.encrypted_private_key.data,
	       sizeof(keygen_response.encrypted_private_key.data));

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_NE(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_load_pairing_key_fail(void)
{
	enum ec_status rv;
	ec_response_fp_establish_pairing_key_keygen keygen_response;
	ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};
	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&wrap_params.encrypted_private_key.info,
	       &keygen_response.encrypted_private_key.info,
	       sizeof(keygen_response.encrypted_private_key.info));

	memcpy(wrap_params.encrypted_private_key.data,
	       keygen_response.encrypted_private_key.data,
	       sizeof(keygen_response.encrypted_private_key.data));

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* No encryption info. */
	memset(&load_params.encrypted_pairing_key.info, 0,
	       sizeof(load_params.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_UNAVAILABLE, "%d");

	/* Cannot be loaded if the context is not cleared. */
	struct ec_params_fp_context_v1 params = {
		.action = FP_CONTEXT_GET_RESULT,
		.userid = { 0, 1, 2, 3, 4, 5, 6, 7 },
	};
	TEST_EQ(test_send_host_command(EC_CMD_FP_CONTEXT, 1, &params,
				       sizeof(params), NULL, 0),
		EC_RES_SUCCESS, "%d");

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_generate_nonce(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_nonce_context(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_nonce_context nonce_params = {};

	fp_reset_and_clear_context();

	templ_valid = 1;

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	TEST_EQ(templ_valid, 1u, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_nonce_context_deny(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_nonce_context nonce_params;

	fp_reset_and_clear_context();

	// Nonce context without generate nonce should fail.
	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	// Generate nonce should clear the existing nonce context user ID.
	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	for (auto user_id_partial : user_id) {
		TEST_EQ(user_id_partial, 0u, "%d");
	}

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_nonce_context_limit_without_generated_nonce(void)
{
	enum ec_status rv;
	struct ec_params_fp_nonce_context nonce_params;

	fp_reset_and_clear_context();

	/* Call nonce context without generated nonce should fail. */
	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_nonce_context_limit_normal_context(void)
{
	enum ec_status rv;
	struct ec_params_fp_context_v1 ctx_params = {
		.action = FP_CONTEXT_GET_RESULT,
	};
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_nonce_context nonce_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Normal context should not clear the generated nonce. */
	/* This will be used for the migration path. */
	rv = test_send_host_command(EC_CMD_FP_CONTEXT, 1, &ctx_params,
				    sizeof(ctx_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Call nonce context with generated nonce should success. */
	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_nonce_context_limit_twice_1(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_nonce_context nonce_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Call nonce context twice should fail. */
	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_nonce_context_limit_twice_2(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_nonce_context nonce_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Call nonce context twice should fail even we generated two nonces. */
	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_nonce_context_load_pk_deny(void)
{
	enum ec_status rv;
	struct ec_response_fp_generate_nonce nonce_response;
	struct ec_params_fp_nonce_context nonce_params;
	struct ec_response_fp_establish_pairing_key_keygen keygen_response;
	struct ec_params_fp_establish_pairing_key_wrap wrap_params {
		.peers_pubkey = {
			.x = {
				0x85, 0xAD, 0x35, 0x23, 0x05, 0x1E, 0x33, 0x3F,
				0xCA, 0xA7, 0xEA, 0xA5, 0x88, 0x33, 0x12, 0x95,
				0xA7, 0xB5, 0x98, 0x9F, 0x32, 0xEF, 0x7D, 0xE9,
				0xF8, 0x70, 0x14, 0x5E, 0x89, 0xCB, 0xDE, 0x1F,
			},
			.y = {
				0xD1, 0xDC, 0x91, 0xC6, 0xE6, 0x5B, 0x1E, 0x3C,
				0x01, 0x6C, 0xE6, 0x50, 0x25, 0x5D, 0x89, 0xCF,
				0xB7, 0x8D, 0x88, 0xB9, 0x0D, 0x09, 0x41, 0xF1,
				0x09, 0x4F, 0x61, 0x55, 0x6C, 0xC4, 0x96, 0x6B,
			},
		},
	};

	ec_response_fp_establish_pairing_key_wrap wrap_response;
	ec_params_fp_load_pairing_key load_params;

	fp_reset_and_clear_context();

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN, 0,
				    NULL, 0, &keygen_response,
				    sizeof(keygen_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&wrap_params.encrypted_private_key.info,
	       &keygen_response.encrypted_private_key.info,
	       sizeof(keygen_response.encrypted_private_key.info));

	memcpy(wrap_params.encrypted_private_key.data,
	       keygen_response.encrypted_private_key.data,
	       sizeof(keygen_response.encrypted_private_key.data));

	rv = test_send_host_command(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP, 0,
				    &wrap_params, sizeof(wrap_params),
				    &wrap_response, sizeof(wrap_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	memcpy(&load_params.encrypted_pairing_key.info,
	       &wrap_response.encrypted_pairing_key.info,
	       sizeof(wrap_response.encrypted_pairing_key.info));

	memcpy(load_params.encrypted_pairing_key.data,
	       wrap_response.encrypted_pairing_key.data,
	       sizeof(wrap_response.encrypted_pairing_key.data));

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_GENERATE_NONCE, 0, NULL, 0,
				    &nonce_response, sizeof(nonce_response));

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	rv = test_send_host_command(EC_CMD_FP_NONCE_CONTEXT, 0, &nonce_params,
				    sizeof(nonce_params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	/* Pairing key cannot be load during a nonce context. */
	rv = test_send_host_command(EC_CMD_FP_LOAD_PAIRING_KEY, 0, &load_params,
				    sizeof(load_params), NULL, 0);

	TEST_EQ(rv, EC_RES_ACCESS_DENIED, "%d");

	return EC_SUCCESS;
}

test_static enum ec_error_list
test_fp_command_read_match_secret_with_pubkey_succeed(void)
{
	struct ec_response_fp_read_match_secret_with_pubkey response = {};
	/* Create valid param with 0 <= fgr < 5 */
	uint16_t matched_fgr = 1;
	struct ec_params_fp_read_match_secret_with_pubkey params = {
		.fgr = matched_fgr,
	};

	/* Expected positive_match_secret same as  in test/fpsensor_crypto.c */
	static const uint8_t
		expected_positive_match_secret_for_empty_user_id[] = {
			0x8d, 0xc4, 0x5b, 0xdf, 0x55, 0x1e, 0xa8, 0x72,
			0xd6, 0xdd, 0xa1, 0x4c, 0xb8, 0xa1, 0x76, 0x2b,
			0xde, 0x38, 0xd5, 0x03, 0xce, 0xe4, 0x74, 0x51,
			0x63, 0x6c, 0x6a, 0x26, 0xa9, 0xb7, 0xfa, 0x68,
		};

	/* Create positive secret match state with valid deadline value,
	 * readable state, and correct template matched
	 */
	struct positive_match_secret_state test_state_1 = {
		.template_matched = matched_fgr,
		.readable = true,
		.deadline = { .val = 5000000 },
	};

	bssl::UniquePtr<EC_KEY> ecdh_key = generate_elliptic_curve_key();

	TEST_NE(ecdh_key.get(), nullptr, "%p");

	std::optional<fp_elliptic_curve_public_key> pubkey =
		create_pubkey_from_ec_key(*ecdh_key);

	TEST_ASSERT(pubkey.has_value());

	params.pubkey = pubkey.value();

	positive_match_secret_state = test_state_1;
	/* Set fp_positive_match_salt to the default fake positive match salt */
	memcpy(fp_positive_match_salt, default_fake_fp_positive_match_salt,
	       sizeof(default_fake_fp_positive_match_salt));

	TEST_ASSERT_ARRAY_EQ(
		(uint8_t const *)fp_positive_match_salt,
		(uint8_t const *)default_fake_fp_positive_match_salt,
		sizeof(default_fake_fp_positive_match_salt));

	/* Initialize an empty user_id to compare positive_match_secret */
	std::fill(std::begin(user_id), std::end(user_id), 0);

	TEST_ASSERT(fp_tpm_seed_is_set());
	/* Test with the correct matched finger state and the default fake
	 * fp_positive_match_salt
	 */
	TEST_ASSERT(
		test_send_host_command(EC_CMD_FP_READ_MATCH_SECRET_WITH_PUBKEY,
				       0, &params, sizeof(params), &response,
				       sizeof(response)) == EC_RES_SUCCESS);

	bssl::UniquePtr<EC_KEY> resp_pubkey =
		create_ec_key_from_pubkey(response.pubkey);

	TEST_NE(resp_pubkey.get(), nullptr, "%p");

	std::array<uint8_t, SHA256_DIGEST_SIZE> enc_key;

	TEST_EQ(generate_ecdh_shared_secret(*ecdh_key, *resp_pubkey,
					    enc_key.data(), enc_key.size()),
		EC_SUCCESS, "%d");

	AES_KEY aes_key;
	std::array<uint8_t, FP_CONTEXT_USERID_IV_LEN> aes_iv;
	std::array<uint8_t, 16> ecount_buf = {};
	unsigned int block_num = 0;

	TEST_EQ(AES_set_encrypt_key(enc_key.data(), 256, &aes_key), 0, "%d");

	std::copy(std::begin(response.iv), std::end(response.iv),
		  aes_iv.begin());

	/* The AES CTR uses the same function for encryption & decryption. */
	AES_ctr128_encrypt(response.enc_secret, response.enc_secret,
			   FP_CONTEXT_USERID_LEN, &aes_key, aes_iv.data(),
			   ecount_buf.data(), &block_num);

	TEST_ASSERT_ARRAY_EQ(
		response.enc_secret,
		expected_positive_match_secret_for_empty_user_id,
		sizeof(expected_positive_match_secret_for_empty_user_id));

	return EC_SUCCESS;
}

test_static enum ec_error_list test_fp_command_preload_template(void)
{
	enum ec_status rv;
	struct ec_params_fp_preload_template params = {
		.offset = 0,
		.size = 0,
		.fgr = 65535,
	};

	rv = test_send_host_command(EC_CMD_FP_PRELOAD_TEMPLATE, 0, &params,
				    sizeof(params), NULL, 0);

	TEST_EQ(rv, EC_RES_OVERFLOW, "%d");

	params.fgr = 0;

	rv = test_send_host_command(EC_CMD_FP_PRELOAD_TEMPLATE, 0, &params,
				    sizeof(params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	params.size |= FP_TEMPLATE_COMMIT;
	rv = test_send_host_command(EC_CMD_FP_PRELOAD_TEMPLATE, 0, &params,
				    sizeof(params), NULL, 0);

	TEST_EQ(rv, EC_RES_SUCCESS, "%d");

	params.size = 0;
	params.offset = 65535;
	rv = test_send_host_command(EC_CMD_FP_PRELOAD_TEMPLATE, 0, &params,
				    sizeof(params), NULL, 0);

	TEST_EQ(rv, EC_RES_INVALID_PARAM, "%d");

	params.size = 1024;
	params.offset = 0;
	rv = test_send_host_command(EC_CMD_FP_PRELOAD_TEMPLATE, 0, &params,
				    sizeof(params), NULL, 0);

	TEST_EQ(rv, EC_RES_REQUEST_TRUNCATED, "%d");

	return EC_SUCCESS;
}

} // namespace

extern "C" void run_test(int argc, const char **argv)
{
	RUN_TEST(test_fp_command_establish_pairing_key_without_seed);
	RUN_TEST(test_fp_command_check_context_cleared);
	RUN_TEST(test_fp_command_generate_nonce);

	// All tests after this require the TPM seed to be set.
	RUN_TEST(test_set_fp_tpm_seed);

	RUN_TEST(test_fp_command_establish_pairing_key_keygen);
	RUN_TEST(test_fp_command_establish_pairing_key_fail);
	RUN_TEST(test_fp_command_establish_and_load_pairing_key);
	RUN_TEST(test_fp_command_load_pairing_key_fail);
	RUN_TEST(test_fp_command_nonce_context);
	RUN_TEST(test_fp_command_nonce_context_deny);
	RUN_TEST(test_fp_command_nonce_context_limit_without_generated_nonce);
	RUN_TEST(test_fp_command_nonce_context_limit_normal_context);
	RUN_TEST(test_fp_command_nonce_context_limit_twice_1);
	RUN_TEST(test_fp_command_nonce_context_limit_twice_2);
	RUN_TEST(test_fp_command_nonce_context_load_pk_deny);
	RUN_TEST(test_fp_command_read_match_secret_with_pubkey_succeed);
	RUN_TEST(test_fp_command_preload_template);
	test_print_result();
}
