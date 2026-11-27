#if UFBXT_IMPL

// Geodesic angle between two orientations in radians, in [0, pi].
// Quaternion sign is irrelevant here, so this measures orientation only.
static double ufbxt_quat_geodesic_angle(ufbx_quat a, ufbx_quat b)
{
	double dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
	dot = fabs(dot);
	if (dot > 1.0) dot = 1.0;
	return 2.0 * acos(dot);
}

// Signed 4-component dot product. A negative value means a discontinuous
// (antipodal) jump between two consecutive stored keys.
static double ufbxt_quat_dot_signed(ufbx_quat a, ufbx_quat b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}

// Accumulated geodesic path length of a sequence of quaternion keys, ignoring
// their stored signs. With coherent signs this equals the distance the
// orientation actually travels (can exceed pi for multi-part turns); it stays
// valid even if a later key was antipodally flipped, so it measures how much
// rotation the key set spans rather than how it was encoded.
static double ufbxt_baked_key_path_length(ufbx_baked_quat_list keys)
{
	double path = 0.0;
	for (size_t i = 1; i < keys.count; i++) {
		double dot = ufbxt_quat_dot_signed(keys.data[i - 1].value, keys.data[i].value);
		double cc = fabs(dot);
		if (cc > 1.0) cc = 1.0;
		path += 2.0 * acos(cc);
	}
	return path;
}

// Signed counterpart of the path length above: it feeds the raw, sign-aware
// dot product into acos instead of its absolute value. A sign flip between two
// keys turns their hemisphere dot negative, so acos jumps past pi/2 and this
// metric rises sharply even though every individual orientation is unchanged.
// This is the encoding-level detector for "numbers continuous, turn wrong".
static double ufbxt_baked_key_signed_path_length(ufbx_baked_quat_list keys)
{
	double path = 0.0;
	for (size_t i = 1; i < keys.count; i++) {
		double dot = ufbxt_quat_dot_signed(keys.data[i - 1].value, keys.data[i].value);
		if (dot > 1.0) dot = 1.0;
		if (dot < -1.0) dot = -1.0;
		path += acos(dot);
	}
	return path;
}

// Model of a runtime consumer that interpolates the baked quaternion channels
// with a plain component-wise lerp followed by normalization, and trusts the
// stored signs (no antipodal fix-up). This is a common playback strategy for
// linearly interpolated baked channels, and it breaks visibly on a sign flip,
// unlike `ufbx_evaluate_baked_quat()` whose internal slerp hides the defect.
static ufbx_quat ufbxt_naive_nlerp(ufbx_quat a, ufbx_quat b, double t)
{
	ufbx_quat r;
	r.x = a.x + (b.x - a.x) * (ufbx_real)t;
	r.y = a.y + (b.y - a.y) * (ufbx_real)t;
	r.z = a.z + (b.z - a.z) * (ufbx_real)t;
	r.w = a.w + (b.w - a.w) * (ufbx_real)t;
	return ufbx_quat_normalize(r);
}

// Dense source-vs-baked playback comparison over [time_begin, time_end].
// Samples landing on (or within 1 us of) a baked key time are "key times",
// all others are "intermediate times"; the two worst angular errors are
// reported separately so each bake setting can be held to its own contract.
static void ufbxt_baked_rotation_playback_error(ufbx_anim *anim, ufbx_node *node,
	ufbx_baked_quat_list keys, double *p_key_error, double *p_mid_error, uint32_t num_samples)
{
	double t0 = anim->time_begin, t1 = anim->time_end;
	double key_error = 0.0, mid_error = 0.0;

	for (uint32_t i = 0; i <= num_samples; i++) {
		double t = t0 + (t1 - t0) * (double)i / (double)num_samples;
		ufbx_quat source = ufbx_evaluate_transform(anim, node, t).rotation;
		ufbx_quat played = ufbx_evaluate_baked_quat(keys, t);
		double angle = ufbxt_quat_geodesic_angle(source, played);

		double nearest_key_dt = 1e9;
		for (size_t k = 0; k < keys.count; k++) {
			double dt = fabs(keys.data[k].time - t);
			if (dt < nearest_key_dt) nearest_key_dt = dt;
		}

		if (nearest_key_dt <= 1e-7) {
			if (angle > key_error) key_error = angle;
		} else {
			if (angle > mid_error) mid_error = angle;
		}
	}

	*p_key_error = key_error;
	*p_mid_error = mid_error;
}

// Every consecutive pair of stored keys must live on the same hemisphere:
// a single non-positive dot is a sign discontinuity. The strict `>= 0`
// boundary also catches an exactly-antipodal pair, where interpolation
// direction is undefined.
static void ufbxt_assert_baked_keys_sign_coherent(ufbx_baked_quat_list keys)
{
	ufbxt_assert(keys.count >= 2);
	for (size_t i = 1; i < keys.count; i++) {
		double dot = ufbxt_quat_dot_signed(keys.data[i - 1].value, keys.data[i].value);
		ufbxt_hintf("rotation key %zu -> %zu dot=%f", i - 1, i, dot);
		ufbxt_assert(dot > 0.0);
	}
}

// Reference path length of the source animation, measured independently on a
// dense sampling of `ufbx_evaluate_transform()`.
static double ufbxt_source_rotation_path_length(ufbx_anim *anim, ufbx_node *node, uint32_t num_samples)
{
	double t0 = anim->time_begin, t1 = anim->time_end;
	double path = 0.0;
	ufbx_quat prev = ufbx_evaluate_transform(anim, node, t0).rotation;
	for (uint32_t i = 1; i <= num_samples; i++) {
		double t = t0 + (t1 - t0) * (double)i / (double)num_samples;
		ufbx_quat cur = ufbx_evaluate_transform(anim, node, t).rotation;
		path += ufbxt_quat_geodesic_angle(prev, cur);
		prev = cur;
	}
	return path;
}

// Locate the named take robustly across exporter variants: the 6100 series
// names it "Spin" while the 7400 binary series prefixes the skeleton name.
static ufbx_anim *ufbxt_find_named_take(ufbx_scene *scene, const char *name)
{
	ufbx_anim_stack *stack = ufbx_find_anim_stack(scene, name);
	if (!stack) {
		char prefixed[128];
		snprintf(prefixed, sizeof(prefixed), "Skeleton|%s", name);
		stack = ufbx_find_anim_stack(scene, prefixed);
	}
	ufbxt_assert(stack);
	ufbxt_assert(stack->anim);
	return stack->anim;
}

// Detect the longest source interval over which the node's orientation stays
// motionless (angular velocity under a small threshold). Returns the interval
// boundaries; a zero-length interval means no pause was found.
static void ufbxt_find_rotation_hold(ufbx_anim *anim, ufbx_node *node,
	uint32_t num_samples, double *p_hold_begin, double *p_hold_end)
{
	double t0 = anim->time_begin, t1 = anim->time_end;
	double best_begin = t0, best_end = t0;
	double cur_begin = t0;
	ufbx_quat prev = ufbx_evaluate_transform(anim, node, t0).rotation;
	double prev_t = t0;

	for (uint32_t i = 1; i <= num_samples; i++) {
		double t = t0 + (t1 - t0) * (double)i / (double)num_samples;
		ufbx_quat cur = ufbx_evaluate_transform(anim, node, t).rotation;
		double rate = ufbxt_quat_geodesic_angle(prev, cur) / (t - prev_t);
		bool still = rate < 1e-3; // rad/s, well below real motion in these takes
		if (still) {
			if (t - cur_begin > best_end - best_begin) {
				best_begin = cur_begin;
				best_end = t;
			}
		} else {
			cur_begin = t;
		}
		prev = cur;
		prev_t = t;
	}

	*p_hold_begin = best_begin;
	*p_hold_end = best_end;
}

#endif
