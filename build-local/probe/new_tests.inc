// A single node swings nearly a full turn (~355 degrees) across the
// quaternion antipodal boundary. File: blender_279_sausage, node "Bottom",
// the "Spin" take (exporter variants name it "Spin" or "Skeleton|Spin"),
// 24 fps linear Euler keys.
UFBXT_FILE_TEST_ALT(baked_quat_sign_coherence_full_turn, blender_279_sausage)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "Bottom");
	ufbxt_assert(node);
	// Use the named take explicitly: the scene's default anim only spans a
	// single-frame static pose in some exporter variants.
	ufbx_anim *anim = ufbxt_find_named_take(scene, "Spin");

	// Source-derived expectation, computed before looking at any baked data:
	// the take really travels almost a full revolution.
	double source_path = ufbxt_source_rotation_path_length(anim, node, 400);
	ufbxt_assert(source_path > 5.5); // > ~315 degrees
	ufbxt_assert(source_path < 2.0 * UFBXT_BAKE_QUAT_PI);

	ufbx_error error;
	ufbx_bake_opts opts = { 0 };
	ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &opts, &error);
	if (!bake) ufbxt_log_error(&error);
	ufbxt_assert(bake);

	ufbx_baked_node *baked_node = ufbx_find_baked_node(bake, node);
	ufbxt_assert(baked_node);
	ufbxt_assert(!baked_node->constant_rotation);
	ufbx_baked_quat_list keys = baked_node->rotation_keys;
	ufbxt_assert(keys.count >= 2);

	// (1) Sign coherence: consecutive stored quaternions share a hemisphere.
	ufbxt_assert_baked_keys_sign_coherent(keys);

	// (2) The stored keys preserve the actual distance travelled.
	double baked_path = ufbxt_baked_key_path_length(keys);
	ufbxt_assert(fabs(baked_path - source_path) < 0.05);

	// (3) Playback fidelity at both key times and intermediate times. This
	// source is itself a 24 fps linear curve, so a 30 fps bake samples it
	// densely enough that slerp playback stays essentially exact.
	double key_error, mid_error;
	ufbxt_baked_rotation_playback_error(anim, node, keys, &key_error, &mid_error, 400);
	ufbxt_assert(key_error < 1e-3);
	ufbxt_assert(mid_error < 1e-3);

	ufbx_free_baked_anim(bake);
}
#endif

// Changing bake settings must not break sign coherence, and the key-time /
// intermediate-time errors must stay within contracts implied by each setting:
// dense sampling stays near-exact, coarser sampling is bounded by its own
// resolution, and key reduction introduces only a small, bounded deviation.
// Same full-turn source as above so antipodal handling is exercised every time.
UFBXT_FILE_TEST_ALT(baked_quat_settings_contracts, blender_279_sausage)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "Bottom");
	ufbxt_assert(node);
	ufbx_anim *anim = ufbxt_find_named_take(scene, "Spin");
	double source_path = ufbxt_source_rotation_path_length(anim, node, 400);
	ufbxt_assert(source_path > 5.5);
	ufbx_error error;

	struct setting_contract {
		ufbx_bake_opts opts;
		double max_mid_error;
	};

	// {0} initialization gives the documented defaults for every other field.
	struct setting_contract contracts[7];
	memset(contracts, 0, sizeof(contracts));
	contracts[0].max_mid_error = 1e-3;                              // default 30 fps
	contracts[1].opts.resample_rate = 60.0;  contracts[1].max_mid_error = 1e-3;
	contracts[2].opts.resample_rate = 15.0;  contracts[2].max_mid_error = 1e-3;
	contracts[3].opts.resample_rate = 24.0;
	contracts[3].opts.key_reduction_enabled = true;
	contracts[3].opts.key_reduction_rotation = true;
	contracts[3].max_mid_error = 2e-3;
	contracts[4].opts.resample_rate = 24.0;
	contracts[4].opts.key_reduction_enabled = true;
	contracts[4].max_mid_error = 2e-3;
	contracts[5].opts.trim_start_time = true;
	contracts[5].max_mid_error = 1e-3;
	contracts[6].opts.no_resample_rotation = true;
	contracts[6].max_mid_error = 1e-3;

	size_t plain_24fps_count = SIZE_MAX;
	size_t prev_key_count = SIZE_MAX;
	for (size_t ci = 0; ci < ufbxt_arraycount(contracts); ci++) {
		ufbxt_hintf("bake setting contract %zu", ci);
		ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &contracts[ci].opts, &error);
		if (!bake) ufbxt_log_error(&error);
		ufbxt_assert(bake);

		ufbx_baked_node *baked_node = ufbx_find_baked_node(bake, node);
		ufbxt_assert(baked_node);
		ufbx_baked_quat_list keys = baked_node->rotation_keys;
		ufbxt_assert(keys.count >= 2);

		ufbxt_assert_baked_keys_sign_coherent(keys);
		ufbxt_assert(fabs(ufbxt_baked_key_path_length(keys) - source_path) < 0.05);

		double key_error, mid_error;
		ufbxt_baked_rotation_playback_error(anim, node, keys, &key_error, &mid_error, 400);
		// Key times reproduce the source pose essentially exactly regardless
		// of setting; intermediate times obey the setting-specific bound.
		ufbxt_assert(key_error < 1e-3);
		ufbxt_assert(mid_error < contracts[ci].max_mid_error);

		// Key reduction must never add keys relative to the equivalent
		// non-reduced 24 fps bake.
		if (ci == 3) plain_24fps_count = prev_key_count;
		if (ci == 3 || ci == 4) {
			ufbxt_assert(keys.count <= plain_24fps_count);
		}
		prev_key_count = keys.count;

		ufbx_free_baked_anim(bake);
	}
}
#endif

// A continuous half-turn (0 -> -180 degrees about Z) whose final orientation
// sits exactly on the quaternion antipodal boundary. Verifies that playback
// reproduces both the turning motion and its timing under resampling changes.
// File: max2009_cube_anim, node "Box01", 2 seconds, 30 fps linear keys; the
// same animation is present identically in the 6100 ascii and binary files.
UFBXT_FILE_TEST_ALT(baked_quat_half_turn_contract, max2009_cube_anim)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "Box01");
	ufbxt_assert(node);
	ufbx_anim *anim = scene->anim;
	ufbxt_assert(anim);
	ufbxt_assert(fabs(anim->time_begin) < 1e-9);
	ufbxt_assert(fabs(anim->time_end - 2.0) < 1e-9);

	double source_path = ufbxt_source_rotation_path_length(anim, node, 600);
	// A half revolution: path and end-to-end chord both approach pi, and
	// because the motion is monotonic they must be nearly equal.
	ufbxt_assert(fabs(source_path - UFBXT_BAKE_QUAT_PI) < 0.02);
	ufbx_quat start_rotation = ufbx_evaluate_transform(anim, node, anim->time_begin).rotation;
	ufbx_quat end_rotation = ufbx_evaluate_transform(anim, node, anim->time_end).rotation;
	ufbxt_assert(fabs(ufbxt_quat_geodesic_angle(start_rotation, end_rotation) - UFBXT_BAKE_QUAT_PI) < 0.02);

	ufbx_error error;

	struct rate_contract {
		double resample_rate;
		double max_mid_error;
	};
	struct rate_contract rate_contracts[] = {
		{ 30.0,  0.002 },
		{ 60.0,  0.002 },
		{ 10.0,  0.012 },
	};

	for (size_t ci = 0; ci < ufbxt_arraycount(rate_contracts); ci++) {
		ufbxt_hintf("half-turn resample contract %zu (rate %g)", ci, rate_contracts[ci].resample_rate);
		ufbx_bake_opts opts = { 0 };
		opts.resample_rate = rate_contracts[ci].resample_rate;
		ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &opts, &error);
		if (!bake) ufbxt_log_error(&error);
		ufbxt_assert(bake);

		ufbx_baked_node *baked_node = ufbx_find_baked_node(bake, node);
		ufbxt_assert(baked_node);
		ufbx_baked_quat_list keys = baked_node->rotation_keys;
		ufbxt_assert(keys.count >= 2);

		ufbxt_assert_baked_keys_sign_coherent(keys);
		ufbxt_assert(fabs(ufbxt_baked_key_path_length(keys) - source_path) < 0.02);

		double key_error, mid_error;
		ufbxt_baked_rotation_playback_error(anim, node, keys, &key_error, &mid_error, 600);
		ufbxt_assert(key_error < 1e-3);
		ufbxt_assert(mid_error < rate_contracts[ci].max_mid_error);

		ufbx_free_baked_anim(bake);
	}

	// Key reduction keeps the same fidelity contract while removing keys.
	{
		ufbx_bake_opts dense_opts = { 0 };
		dense_opts.resample_rate = 24.0;
		ufbx_baked_anim *dense_bake = ufbx_bake_anim(scene, anim, &dense_opts, &error);
		ufbxt_assert(dense_bake);
		size_t dense_count = ufbx_find_baked_node(dense_bake, node)->rotation_keys.count;

		ufbx_bake_opts reduced_opts = { 0 };
		reduced_opts.resample_rate = 24.0;
		reduced_opts.key_reduction_enabled = true;
		reduced_opts.key_reduction_rotation = true;
		ufbx_baked_anim *reduced_bake = ufbx_bake_anim(scene, anim, &reduced_opts, &error);
		ufbxt_assert(reduced_bake);
		ufbx_baked_quat_list reduced_keys = ufbx_find_baked_node(reduced_bake, node)->rotation_keys;

		ufbxt_assert(reduced_keys.count <= dense_count);
		ufbxt_assert_baked_keys_sign_coherent(reduced_keys);
		double key_error, mid_error;
		ufbxt_baked_rotation_playback_error(anim, node, reduced_keys, &key_error, &mid_error, 600);
		ufbxt_assert(key_error < 1e-3);
		ufbxt_assert(mid_error < 0.005);

		ufbx_free_baked_anim(dense_bake);
		ufbx_free_baked_anim(reduced_bake);
	}
}
#endif

// A rotating node followed by a long, motionless hold, in an animation whose
// default take is identical across all available exporter variants. File:
// maya_instanced_skin, node "pCube2", ~135 degrees of turning then a 0.5 s
// pause (60% of the clip). Verifies the pause is preserved by baking and
// playback rather than being smoothed into motion.
UFBXT_FILE_TEST(maya_instanced_skin)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "pCube2");
	ufbxt_assert(node);
	ufbx_anim *anim = scene->anim;
	ufbxt_assert(anim);

	// Locate the pause independently from the source animation.
	double hold_begin_time, hold_end_time;
	ufbxt_find_rotation_hold(anim, node, 400, &hold_begin_time, &hold_end_time);
	ufbxt_assert(hold_end_time - hold_begin_time > 0.3);
	ufbx_quat held_rotation = ufbx_evaluate_transform(anim, node, hold_begin_time).rotation;

	double source_hold_variation = 0.0;
	for (uint32_t i = 0; i <= 20; i++) {
		double t = hold_begin_time + (hold_end_time - hold_begin_time) * (double)i / 20.0;
		ufbx_quat q = ufbx_evaluate_transform(anim, node, t).rotation;
		source_hold_variation = fmax(source_hold_variation, ufbxt_quat_geodesic_angle(held_rotation, q));
	}
	ufbxt_assert(source_hold_variation < 1e-3);

	ufbx_error error;
	ufbx_bake_opts opts = { 0 };
	ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &opts, &error);
	if (!bake) ufbxt_log_error(&error);
	ufbxt_assert(bake);
	ufbx_baked_node *baked_node = ufbx_find_baked_node(bake, node);
	ufbxt_assert(baked_node);
	ufbx_baked_quat_list keys = baked_node->rotation_keys;
	ufbxt_assert(keys.count >= 2);
	ufbxt_assert_baked_keys_sign_coherent(keys);

	// Overall key-time fidelity.
	double key_error, mid_error;
	ufbxt_baked_rotation_playback_error(anim, node, keys, &key_error, &mid_error, 400);
	ufbxt_assert(key_error < 1e-3);
	ufbxt_assert(mid_error < 0.03);

	// The pause survives playback: across the held interval the baked
	// evaluation must remain at the held orientation instead of drifting.
	double baked_hold_variation = 0.0;
	for (uint32_t i = 0; i <= 20; i++) {
		double t = hold_begin_time + (hold_end_time - hold_begin_time) * (double)i / 20.0;
		ufbx_quat q = ufbx_evaluate_baked_quat(keys, t);
		baked_hold_variation = fmax(baked_hold_variation, ufbxt_quat_geodesic_angle(held_rotation, q));
	}
	ufbxt_assert(baked_hold_variation < 1e-3);

	ufbx_free_baked_anim(bake);
}
#endif

// Negative control: prove the sign-coherence contracts actually reject a
// "numerically continuous, wrong turning direction" result instead of
// accepting whatever the baker currently emits. A healthy full-turn bake is
// validated first; then one interior key is antipodally flipped (q -> -q).
// Every key-time orientation stays identical, so the defect is invisible to
// orientation sampling, but (a) the stored sign/path checks trip, and (b) a
// sign-trusting playback consumer rotates the wrong way at intermediate times.
UFBXT_FILE_TEST_ALT(baked_quat_detects_antipodal_flip, blender_279_sausage)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "Bottom");
	ufbxt_assert(node);
	ufbx_anim *anim = ufbxt_find_named_take(scene, "Spin");

	ufbx_error error;
	ufbx_bake_opts opts = { 0 };
	ufbx_baked_anim *bake = ufbx_bake_anim(scene, anim, &opts, &error);
	if (!bake) ufbxt_log_error(&error);
	ufbxt_assert(bake);
	ufbx_baked_node *baked_node = ufbx_find_baked_node(bake, node);
	ufbxt_assert(baked_node);
	ufbx_baked_quat_list keys = baked_node->rotation_keys;
	ufbxt_assert(keys.count >= 4);

	// Baseline metrics on the healthy bake.
	ufbxt_assert_baked_keys_sign_coherent(keys);
	double healthy_signed_path = ufbxt_baked_key_signed_path_length(keys);
	ufbxt_assert(healthy_signed_path > 0.0);

	size_t flip_ix = keys.count / 2;
	double flip_time = keys.data[flip_ix].time;
	double prev_time = keys.data[flip_ix - 1].time;
	double mid_time = 0.5 * (prev_time + flip_time);
	ufbx_quat expected_mid = ufbx_evaluate_transform(anim, node, mid_time).rotation;

	// A sign-trusting playback consumer interpolates correctly before mutation.
	ufbx_quat healthy_nlerp = ufbxt_naive_nlerp(
		keys.data[flip_ix - 1].value, keys.data[flip_ix].value, 0.5);
	ufbxt_assert(ufbxt_quat_geodesic_angle(expected_mid, healthy_nlerp) < 1e-3);

	// Mutate: negate every component of one interior key. Orientations at key
	// times stay identical (q and -q rotate the same way).
	keys.data[flip_ix].value.x = -keys.data[flip_ix].value.x;
	keys.data[flip_ix].value.y = -keys.data[flip_ix].value.y;
	keys.data[flip_ix].value.z = -keys.data[flip_ix].value.z;
	keys.data[flip_ix].value.w = -keys.data[flip_ix].value.w;

	// (1) The stored-key sign check flags both affected boundaries.
	ufbxt_assert(ufbxt_quat_dot_signed(keys.data[flip_ix - 1].value, keys.data[flip_ix].value) < 0.0);
	ufbxt_assert(ufbxt_quat_dot_signed(keys.data[flip_ix].value, keys.data[flip_ix + 1].value) < 0.0);

	// (2) The signed path metric jumps (two acos terms cross pi/2), while the
	// sign-insensitive geometric path is unchanged. Together these prove the
	// defect is purely in the chosen quaternion signs, not the orientations.
	double mutated_signed_path = ufbxt_baked_key_signed_path_length(keys);
	ufbxt_assert(mutated_signed_path > healthy_signed_path + 1.0);

	// (3) A sign-trusting playback consumer now takes the wrong-way arc: the
	// midpoint lands ~180 degrees off the source-derived expectation. This is
	// the precise "continuous numbers, wrong turn" failure the suite guards.
	ufbx_quat mutated_nlerp = ufbxt_naive_nlerp(
		keys.data[flip_ix - 1].value, keys.data[flip_ix].value, 0.5);
	double flip_mid_error = ufbxt_quat_geodesic_angle(expected_mid, mutated_nlerp);
	ufbxt_assert(flip_mid_error > 2.5); // > ~143 degrees

	// (4) Key-time orientations remain valid, demonstrating why sign coherence
	// has to be asserted on the stored representation rather than on samples.
	ufbx_quat source_at_key = ufbx_evaluate_transform(anim, node, flip_time).rotation;
	ufbxt_assert(ufbxt_quat_geodesic_angle(keys.data[flip_ix].value, source_at_key) < 1e-3);

	ufbx_free_baked_anim(bake);
}
#endif
