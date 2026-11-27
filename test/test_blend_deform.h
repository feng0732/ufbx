#undef UFBXT_TEST_GROUP
#define UFBXT_TEST_GROUP "blend"

// Tests for blend (morph/progressive) deformer import and evaluation.
//
// A FBX progressive blend channel maps a single driver weight onto the
// effective contributions of one or more target shapes: keyframes are sorted
// by `target_weight` and, for any driver weight, the two keyframes bracketing
// that weight (with driver weight 0.0 acting as an implicit zero baseline)
// are linearly interpolated and extrapolated beyond the keyframe range.
// The displaced position of a control point is therefore determined
// independently by the source target shapes and their weight relationship:
//
//   pos'(v) = pos(v) + sum_channel sum_key effective_weight(k)
//                              * offset_weight(k, v) * position_offset(k, v)
//
// The same deformation is exposed through the channel animation
// (`ufbx_evaluate_blend_weight` + evaluated scene channels) and through the
// mesh displacement query/batch APIs, so both paths must describe the same
// geometry. All expectations below are derived from the source shapes and
// weights themselves, never from existing library output.

#if UFBXT_IMPL

// Bounds of the test data used by this file: every mesh has at most this
// many control points and every channel at most this many keyframes.
enum {
	UFBXT_BLEND_TEST_MAX_VERTICES = 32,
	UFBXT_BLEND_TEST_MAX_KEYFRAMES = 4,
};

// Find a blend channel of a deformer by name substring.
static ufbx_blend_channel *ufbxt_find_blend_channel(ufbx_blend_deformer *deformer, const char *name)
{
	for (size_t i = 0; i < deformer->channels.count; i++) {
		ufbx_blend_channel *channel = deformer->channels.data[i];
		if (strstr(channel->name.data, name)) return channel;
	}
	return NULL;
}

// Reference implementation of the progressive channel contract.
//
// The driver weight 0.0 is an implicit baseline contributing no shape.
// Keyframes are ordered by target weight and, depending on the sign of the
// driver weight, the two keyframes bracketing it are linearly interpolated;
// outside the explicit keyframe range the line through the implicit zero
// baseline and the nearest keyframe is extrapolated, so effective weights
// may be negative or greater than one. This mirrors the source deformation
// semantics rather than clamping to the first/last target shape.
static void ufbxt_ref_effective_weights(const ufbx_blend_channel *channel, ufbx_real weight, ufbx_real *dst)
{
	size_t count = channel->keyframes.count;
	for (size_t i = 0; i < count; i++) dst[i] = 0.0f;
	if (count == 0) return;

	const ufbx_blend_keyframe *keys = channel->keyframes.data;

	// Keyframes are ordered by target_weight on import.
	for (size_t i = 1; i < count; i++) {
		ufbxt_assert(keys[i - 1].target_weight <= keys[i].target_weight);
	}

	// Locate the last keyframe with a negative target weight, if any.
	ptrdiff_t last_negative = -1;
	for (ptrdiff_t i = 0; i < (ptrdiff_t)count; i++) {
		if (keys[i].target_weight < 0.0f) last_negative = i;
	}

	// Bracketing keyframes. A null slot represents the implicit zero
	// baseline at target weight 0.0 with no associated shape contribution.
	const ufbx_blend_keyframe zero_key = { 0 };
	const ufbx_blend_keyframe *prev = &zero_key;
	const ufbx_blend_keyframe *next = &zero_key;
	ptrdiff_t prev_ix = -1, next_ix = -1;

	if (weight > 0.0f) {
		for (ptrdiff_t i = last_negative + 1; i < (ptrdiff_t)count; i++) {
			prev = next;
			prev_ix = next_ix;
			next = &keys[i];
			next_ix = i;
			if (next->target_weight > weight) break;
		}
	} else {
		if (last_negative + 1 < (ptrdiff_t)count) {
			prev = &keys[last_negative + 1];
			prev_ix = last_negative + 1;
		}
		for (ptrdiff_t i = last_negative; i >= 0; i--) {
			prev = next;
			prev_ix = next_ix;
			next = &keys[i];
			next_ix = i;
			if (next->target_weight < weight) break;
		}
	}

	ufbx_real delta = next->target_weight - prev->target_weight;
	if (delta != 0.0f) {
		ufbx_real t = (weight - prev->target_weight) / delta;
		if (prev_ix >= 0) dst[prev_ix] = 1.0f - t;
		if (next_ix >= 0) dst[next_ix] = t;
	}
}

// Reference raw per-shape vertex offset as stored in the source shape.
// Returns zero for control points not referenced by the shape. The optional
// Blender per-offset weights are applied separately by the batch API only,
// matching `ufbx_get_blend_shape_vertex_offset`.
static ufbx_vec3 ufbxt_ref_shape_vertex_offset(const ufbx_blend_shape *shape, size_t vertex)
{
	uint32_t vertex_ix = (uint32_t)vertex;
	for (size_t i = 0; i < shape->num_offsets; i++) {
		if (shape->offset_vertices.data[i] == vertex_ix) {
			return shape->position_offsets.data[i];
		}
	}
	return ufbx_zero_vec3;
}

// Reference displacement of a single control point for one target shape at
// a given effective keyframe weight, including the optional Blender-style
// per-offset weights that scale individual control points.
static ufbx_vec3 ufbxt_ref_shape_weighted_offset(const ufbx_blend_shape *shape,
	ufbx_real effective_weight, size_t vertex)
{
	uint32_t vertex_ix = (uint32_t)vertex;
	for (size_t i = 0; i < shape->num_offsets; i++) {
		if (shape->offset_vertices.data[i] == vertex_ix) {
			ufbx_real per_vertex_weight = effective_weight;
			if (i < shape->offset_weights.count) {
				per_vertex_weight *= shape->offset_weights.data[i];
			}
			return ufbxt_mul3(shape->position_offsets.data[i], per_vertex_weight);
		}
	}
	return ufbx_zero_vec3;
}

// Reference displacement of a single control point for one channel at
// a given driver weight, derived directly from the source target shapes.
static ufbx_vec3 ufbxt_ref_channel_vertex_offset(const ufbx_blend_channel *channel,
	ufbx_real weight, size_t vertex)
{
	ufbx_real effective[UFBXT_BLEND_TEST_MAX_KEYFRAMES];
	ufbxt_assert(channel->keyframes.count <= UFBXT_BLEND_TEST_MAX_KEYFRAMES);
	ufbxt_ref_effective_weights(channel, weight, effective);

	ufbx_vec3 result = ufbx_zero_vec3;
	for (size_t k = 0; k < channel->keyframes.count; k++) {
		if (effective[k] == 0.0f) continue;
		ufbx_vec3 offset = ufbxt_ref_shape_weighted_offset(channel->keyframes.data[k].shape,
			effective[k], vertex);
		result = ufbxt_add3(result, offset);
	}
	return result;
}

// Reference displacement of a single control point for a whole deformer.
static ufbx_vec3 ufbxt_ref_deformer_vertex_offset(const ufbx_blend_deformer *deformer,
	const ufbx_real *channel_weights, size_t vertex)
{
	ufbx_vec3 result = ufbx_zero_vec3;
	for (size_t c = 0; c < deformer->channels.count; c++) {
		ufbx_vec3 offset = ufbxt_ref_channel_vertex_offset(deformer->channels.data[c],
			channel_weights[c], vertex);
		result = ufbxt_add3(result, offset);
	}
	return result;
}

// Reference batch evaluation of every control point for a deformer.
static void ufbxt_ref_deformer_offsets(const ufbx_blend_deformer *deformer,
	const ufbx_real *channel_weights, ufbx_vec3 *dst, size_t num_vertices)
{
	ufbxt_assert(num_vertices <= UFBXT_BLEND_TEST_MAX_VERTICES);
	for (size_t v = 0; v < num_vertices; v++) {
		dst[v] = ufbxt_ref_deformer_vertex_offset(deformer, channel_weights, v);
	}
}

// Apply the reference progressive decomposition manually through the public
// per-shape batch API, yielding displaced positions for all control points.
// This only relies on `ufbx_add_blend_shape_vertex_offsets`, so it can be
// evaluated at arbitrary driver weights independent of any scene state.
static void ufbxt_apply_ref_offsets(const ufbx_blend_deformer *deformer,
	const ufbx_real *channel_weights, ufbx_vec3 *positions, size_t num_vertices)
{
	for (size_t c = 0; c < deformer->channels.count; c++) {
		const ufbx_blend_channel *channel = deformer->channels.data[c];
		ufbx_real effective[UFBXT_BLEND_TEST_MAX_KEYFRAMES];
		ufbxt_assert(channel->keyframes.count <= UFBXT_BLEND_TEST_MAX_KEYFRAMES);
		ufbxt_ref_effective_weights(channel, channel_weights[c], effective);
		for (size_t k = 0; k < channel->keyframes.count; k++) {
			if (effective[k] == 0.0f) continue;
			ufbx_add_blend_shape_vertex_offsets(channel->keyframes.data[k].shape,
				positions, num_vertices, effective[k]);
		}
	}
}

// Mark every control point referenced by any shape of the deformer.
static void ufbxt_collect_affected_vertices(const ufbx_blend_deformer *deformer,
	bool *affected, size_t num_vertices)
{
	for (size_t v = 0; v < num_vertices; v++) affected[v] = false;
	for (size_t c = 0; c < deformer->channels.count; c++) {
		const ufbx_blend_channel *channel = deformer->channels.data[c];
		for (size_t k = 0; k < channel->keyframes.count; k++) {
			const ufbx_blend_shape *shape = channel->keyframes.data[k].shape;
			for (size_t i = 0; i < shape->num_offsets; i++) {
				uint32_t vertex = shape->offset_vertices.data[i];
				ufbxt_assert(vertex < num_vertices);
				affected[vertex] = true;
			}
		}
	}
}

// Sparse shape-level index/offset query contract: listed vertices resolve
// to their stored offset, everything else to no index and zero offset.
static void ufbxt_check_sparse_shape_queries(ufbxt_diff_error *err,
	const ufbx_blend_deformer *deformer, size_t num_vertices)
{
	for (size_t c = 0; c < deformer->channels.count; c++) {
		const ufbx_blend_channel *channel = deformer->channels.data[c];
		for (size_t k = 0; k < channel->keyframes.count; k++) {
			const ufbx_blend_shape *shape = channel->keyframes.data[k].shape;
			for (size_t i = 0; i < shape->num_offsets; i++) {
				uint32_t vertex = shape->offset_vertices.data[i];
				uint32_t index = ufbx_get_blend_shape_offset_index(shape, vertex);
				ufbxt_assert(index != UFBX_NO_INDEX);
				ufbxt_assert(shape->offset_vertices.data[index] == vertex);
				ufbxt_assert_close_vec3(err,
					ufbx_get_blend_shape_vertex_offset(shape, vertex),
					shape->position_offsets.data[index]);
			}
			for (size_t v = 0; v < num_vertices; v++) {
				bool listed = false;
				for (size_t i = 0; i < shape->num_offsets; i++) {
					if (shape->offset_vertices.data[i] == (uint32_t)v) listed = true;
				}
				if (!listed) {
					ufbxt_assert(ufbx_get_blend_shape_offset_index(shape, v) == UFBX_NO_INDEX);
					ufbxt_assert_vec3_equal(ufbx_get_blend_shape_vertex_offset(shape, v),
						0.0f, 0.0f, 0.0f);
				}
			}
		}
	}
}

// Geometry check usable for arbitrary driver weights on a scene whose
// deformer keyframes may carry different stored effective weights: the
// reference displacement computed from the source contract is compared
// against an independent assembly using only the per-shape batch API
// (which honors per-offset weights), and unaffected control points are
// asserted to stay in place. The raw shape-level query is separately
// validated against the stored source offsets.
static void ufbxt_check_reference_displacement(ufbxt_diff_error *err, const ufbx_mesh *mesh,
	const ufbx_blend_deformer *deformer, const ufbx_real *channel_weights)
{
	size_t num_vertices = mesh->num_vertices;
	ufbxt_assert(num_vertices <= UFBXT_BLEND_TEST_MAX_VERTICES);

	ufbx_vec3 ref_offsets[UFBXT_BLEND_TEST_MAX_VERTICES];
	ufbxt_ref_deformer_offsets(deformer, channel_weights, ref_offsets, num_vertices);

	ufbx_vec3 positions[UFBXT_BLEND_TEST_MAX_VERTICES];
	memcpy(positions, mesh->vertices.data, num_vertices * sizeof(ufbx_vec3));
	ufbxt_apply_ref_offsets(deformer, channel_weights, positions, num_vertices);

	bool affected[UFBXT_BLEND_TEST_MAX_VERTICES];
	ufbxt_collect_affected_vertices(deformer, affected, num_vertices);

	for (size_t v = 0; v < num_vertices; v++) {
		ufbx_vec3 expected = ufbxt_add3(mesh->vertices.data[v], ref_offsets[v]);
		ufbxt_assert_close_vec3(err, positions[v], expected);

		// The raw per-vertex shape query always returns the stored source
		// offset for listed control points and zero for unlisted ones.
		for (size_t c = 0; c < deformer->channels.count; c++) {
			const ufbx_blend_channel *channel = deformer->channels.data[c];
			for (size_t k = 0; k < channel->keyframes.count; k++) {
				const ufbx_blend_shape *shape = channel->keyframes.data[k].shape;
				ufbx_vec3 query = ufbx_get_blend_shape_vertex_offset(shape, v);
				ufbx_vec3 reference = ufbxt_ref_shape_vertex_offset(shape, v);
				ufbxt_assert_close_vec3(err, query, reference);
			}
		}

		if (!affected[v]) {
			// Control points outside every target shape stay exactly in place.
			ufbxt_assert_vec3_equal(ref_offsets[v], 0.0f, 0.0f, 0.0f);
			ufbxt_assert_vec3_equal(positions[v],
				mesh->vertices.data[v].x, mesh->vertices.data[v].y, mesh->vertices.data[v].z);
		}
	}

	ufbxt_check_sparse_shape_queries(err, deformer, num_vertices);
}

// Verify that an evaluated deformer decomposes each animated driver weight
// into the source-contract effective weights computed from the target
// weights: bracketing keyframes interpolate linearly, all others contribute
// nothing, and outside the keyframe range the implicit zero baseline is
// extrapolated.
static void ufbxt_check_progressive_contract(ufbxt_diff_error *err,
	const ufbx_blend_deformer *deformer, const ufbx_real *driver_weights)
{
	for (size_t c = 0; c < deformer->channels.count; c++) {
		const ufbx_blend_channel *channel = deformer->channels.data[c];
		ufbx_real weight = driver_weights[c];

		ufbx_real ref_effective[UFBXT_BLEND_TEST_MAX_KEYFRAMES];
		ufbxt_assert(channel->keyframes.count <= UFBXT_BLEND_TEST_MAX_KEYFRAMES);
		ufbxt_ref_effective_weights(channel, weight, ref_effective);

		for (size_t k = 0; k < channel->keyframes.count; k++) {
			ufbxt_assert_close_real(err, channel->keyframes.data[k].effective_weight, ref_effective[k]);
		}

		// At the zero driver weight the implicit baseline applies and no
		// target shape receives any contribution.
		if (weight == 0.0f) {
			for (size_t k = 0; k < channel->keyframes.count; k++) {
				ufbxt_assert_close_real(err, ref_effective[k], 0.0f);
			}
		}
	}
}

// Whether any target shape of the deformer carries Blender-style per-offset
// control point weights. These scale individual control points during batch
// and skinned evaluation but are not reflected by the per-vertex query API.
static bool ufbxt_deformer_has_offset_weights(const ufbx_blend_deformer *deformer)
{
	for (size_t c = 0; c < deformer->channels.count; c++) {
		const ufbx_blend_channel *channel = deformer->channels.data[c];
		for (size_t k = 0; k < channel->keyframes.count; k++) {
			if (channel->keyframes.data[k].shape->offset_weights.count > 0) return true;
		}
	}
	return false;
}

// Full evaluated-scene check: the batch APIs must agree with the
// source-derived displacement for the stored driver weights, the evaluated
// skinned mesh must describe the same geometry, and on deformer
// representations without per-offset weights the per-vertex query API must
// report the identical geometric displacement.
static void ufbxt_check_evaluated_geometry(ufbxt_diff_error *err, const ufbx_mesh *mesh,
	const ufbx_blend_deformer *deformer, const ufbx_real *channel_weights)
{
	size_t num_vertices = mesh->num_vertices;
	ufbxt_assert(num_vertices <= UFBXT_BLEND_TEST_MAX_VERTICES);

	ufbx_vec3 ref_offsets[UFBXT_BLEND_TEST_MAX_VERTICES];
	ufbxt_ref_deformer_offsets(deformer, channel_weights, ref_offsets, num_vertices);

	bool has_offset_weights = ufbxt_deformer_has_offset_weights(deformer);

	// Deformer-level per-vertex query. Without per-offset weights this is
	// geometrically identical to the batch path; with them the query returns
	// the unweighted target offsets while the reference displacement scales
	// each control point, so the equivalence is validated through the batch
	// and skinned paths below instead.
	if (!has_offset_weights) {
		for (size_t v = 0; v < num_vertices; v++) {
			ufbx_vec3 query_offset = ufbx_get_blend_vertex_offset(deformer, v);
			ufbxt_assert_close_vec3(err, query_offset, ref_offsets[v]);
		}
	}

	// Deformer-level batch API against the source-derived reference.
	ufbx_vec3 batch_positions[UFBXT_BLEND_TEST_MAX_VERTICES];
	memcpy(batch_positions, mesh->vertices.data, num_vertices * sizeof(ufbx_vec3));
	ufbx_add_blend_vertex_offsets(deformer, batch_positions, num_vertices, 1.0f);
	for (size_t v = 0; v < num_vertices; v++) {
		ufbx_vec3 batch_offset = ufbxt_sub3(batch_positions[v], mesh->vertices.data[v]);
		ufbxt_assert_close_vec3(err, batch_offset, ref_offsets[v]);
	}

	// Shape-by-shape assembly must produce the same result as the deformer.
	ufbx_vec3 shape_positions[UFBXT_BLEND_TEST_MAX_VERTICES];
	memcpy(shape_positions, mesh->vertices.data, num_vertices * sizeof(ufbx_vec3));
	ufbxt_apply_ref_offsets(deformer, channel_weights, shape_positions, num_vertices);
	for (size_t v = 0; v < num_vertices; v++) {
		ufbxt_assert_close_vec3(err, shape_positions[v], batch_positions[v]);
	}

	bool affected[UFBXT_BLEND_TEST_MAX_VERTICES];
	ufbxt_collect_affected_vertices(deformer, affected, num_vertices);
	for (size_t v = 0; v < num_vertices; v++) {
		if (!affected[v]) {
			ufbxt_assert_vec3_equal(ufbx_get_blend_vertex_offset(deformer, v), 0.0f, 0.0f, 0.0f);
			ufbxt_assert_vec3_equal(batch_positions[v],
				mesh->vertices.data[v].x, mesh->vertices.data[v].y, mesh->vertices.data[v].z);
		}
	}

	ufbxt_check_sparse_shape_queries(err, deformer, num_vertices);
}

// Find the mesh that owns an evaluated blend deformer.
static ufbx_mesh *ufbxt_find_deformer_mesh(ufbx_scene *eval, const ufbx_blend_deformer *eval_deformer)
{
	for (size_t m = 0; m < eval->meshes.count; m++) {
		ufbx_mesh *candidate = eval->meshes.data[m];
		for (size_t d = 0; d < candidate->blend_deformers.count; d++) {
			if (candidate->blend_deformers.data[d] == eval_deformer) {
				return candidate;
			}
		}
	}
	return NULL;
}

// Evaluate the scene at `time` and validate that the animated channel driver
// weights and the resulting mesh displacement are two views of the same
// source deformation contract.
static void ufbxt_check_animated_frame(ufbxt_diff_error *err, ufbx_scene *scene,
	const ufbx_blend_deformer *deformer, double time)
{
	ufbx_real driver_weights[UFBXT_BLEND_TEST_MAX_KEYFRAMES];
	ufbxt_assert(deformer->channels.count <= UFBXT_BLEND_TEST_MAX_KEYFRAMES);
	for (size_t c = 0; c < deformer->channels.count; c++) {
		driver_weights[c] = ufbx_evaluate_blend_weight(scene->anim, deformer->channels.data[c], time);
	}

	ufbx_evaluate_opts opts = { 0 };
	opts.evaluate_skinning = true;
	ufbx_scene *eval = ufbx_evaluate_scene(scene, scene->anim, time, &opts, NULL);
	ufbxt_assert(eval);

	// The evaluated channels carry exactly the animated driver weights.
	for (size_t c = 0; c < deformer->channels.count; c++) {
		ufbx_blend_channel *source = deformer->channels.data[c];
		ufbx_blend_channel *evaluated = eval->blend_channels.data[source->typed_id];
		ufbxt_assert(evaluated);
		ufbxt_assert_close_real(err, evaluated->weight, driver_weights[c]);
	}

	ufbx_blend_deformer *eval_deformer = eval->blend_deformers.data[deformer->typed_id];
	ufbx_mesh *target_mesh = ufbxt_find_deformer_mesh(eval, eval_deformer);
	ufbxt_assert(target_mesh);

	ufbxt_check_progressive_contract(err, eval_deformer, driver_weights);
	ufbxt_check_evaluated_geometry(err, target_mesh, eval_deformer, driver_weights);

	// Channel animation and mesh displacement describe the same process:
	// every skinned control point equals base position plus the offset
	// derived independently from the source target shapes and weights.
	for (size_t v = 0; v < target_mesh->num_vertices; v++) {
		ufbx_vec3 ref_offset = ufbxt_ref_deformer_vertex_offset(eval_deformer, driver_weights, v);
		ufbx_vec3 ref_position = ufbxt_add3(target_mesh->vertex_position.values.data[v], ref_offset);
		ufbxt_assert_close_vec3(err, target_mesh->skinned_position.values.data[v], ref_position);
	}

	ufbxt_check_scene(eval);
	ufbx_free_scene(eval);
}

#endif

// Maya progressive blend shape: two channels, one with two in-between
// targets at positive target weights, the other with three targets
// including negative target weights. Exercises extrapolation beyond the
// keyframe range on both sides, interpolation across every adjacent pair of
// target shapes and sparse shape offsets (only a subset of the 26 control
// points is affected).
UFBXT_FILE_TEST_ALT(maya_blend_inbetween_progressive, maya_blend_inbetween)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "pCube1");
	ufbxt_assert(node && node->mesh);
	ufbx_mesh *mesh = node->mesh;
	ufbxt_assert(mesh->num_vertices == 26);
	ufbxt_assert(mesh->blend_deformers.count == 1);

	ufbx_blend_deformer *deformer = mesh->blend_deformers.data[0];
	ufbxt_assert(deformer->channels.count == 2);

	ufbx_blend_channel *lift = ufbxt_find_blend_channel(deformer, "LiftTop");
	ufbx_blend_channel *move = ufbxt_find_blend_channel(deformer, "MoveToSide");
	ufbxt_assert(lift && move);

	// Import contract: progressive channels expose every target shape with
	// its source target weight, and the final target shape is the last key.
	ufbxt_assert(lift->keyframes.count == 2);
	ufbxt_assert_close_real(err, lift->keyframes.data[0].target_weight, 0.5f);
	ufbxt_assert_close_real(err, lift->keyframes.data[1].target_weight, 1.0f);
	ufbxt_assert(lift->target_shape == lift->keyframes.data[1].shape);
	ufbxt_assert(lift->keyframes.data[0].shape->num_offsets == 4);
	ufbxt_assert(lift->keyframes.data[1].shape->num_offsets == 8);

	ufbxt_assert(move->keyframes.count == 3);
	ufbxt_assert_close_real(err, move->keyframes.data[0].target_weight, -0.499f);
	ufbxt_assert_close_real(err, move->keyframes.data[1].target_weight, -0.249f);
	ufbxt_assert_close_real(err, move->keyframes.data[2].target_weight, 1.0f);
	ufbxt_assert(move->target_shape == move->keyframes.data[2].shape);

	// Driver weights landing below the first target, inside every
	// interpolation interval (including negative ones), exactly on a target
	// and above the last target.
	static const ufbx_real driver_pairs[][2] = {
		{ -1.0f,   2.0f   },
		{ -0.499f, 1.0f   },
		{  0.0f,   0.5f   },
		{  0.5f,  -0.249f },
		{  0.75f,  0.0f   },
		{  1.0f,  -0.499f },
		{  1.5f,  -1.0f   },
		{  2.0f,  -2.0f   },
	};

	for (size_t sample = 0; sample < ufbxt_arraycount(driver_pairs); sample++) {
		const ufbx_real *weights = driver_pairs[sample];

		// Displaced geometry derived independently from the source contract
		// and assembled through the public per-shape batch API.
		ufbxt_check_reference_displacement(err, mesh, deformer, weights);
	}

	// Animation path: evaluated driver weights decompose into the same
	// progressive keyframe contributions, and the skinned mesh displacement
	// equals the independently derived offsets at every reference frame.
	static const double frame_times[] = {
		1.0/24.0, 30.0/24.0, 60.0/24.0, 65.0/24.0,
		71.0/24.0, 80.0/24.0, 89.0/24.0, 120.0/24.0,
	};
	for (size_t f = 0; f < ufbxt_arraycount(frame_times); f++) {
		ufbxt_check_animated_frame(err, scene, deformer, frame_times[f]);
	}
}
#endif

// Simple (non-progressive) channels: each channel has exactly one target
// shape at target weight 1.0, so the effective weight equals the driver
// weight and multiple channels add independently.
UFBXT_FILE_TEST_ALT(maya_blend_shape_cube_progressive, maya_blend_shape_cube)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "pCube1");
	ufbxt_assert(node && node->mesh);
	ufbx_mesh *mesh = node->mesh;
	ufbxt_assert(mesh->num_vertices == 8);
	ufbxt_assert(mesh->blend_deformers.count == 1);

	ufbx_blend_deformer *deformer = mesh->blend_deformers.data[0];
	ufbxt_assert(deformer->channels.count == 2);

	ufbx_blend_channel *top_h = ufbxt_find_blend_channel(deformer, "TopH");
	ufbx_blend_channel *top_v = ufbxt_find_blend_channel(deformer, "TopV");
	ufbxt_assert(top_h && top_v);

	for (size_t c = 0; c < deformer->channels.count; c++) {
		ufbx_blend_channel *channel = deformer->channels.data[c];
		ufbxt_assert(channel->keyframes.count == 1);
		ufbxt_assert_close_real(err, channel->keyframes.data[0].target_weight, 1.0f);
		ufbxt_assert(channel->target_shape == channel->keyframes.data[0].shape);
	}

	// A single keyframe maps any driver weight fully onto its target shape.
	static const ufbx_real probe_weights[] = { 0.0f, 0.25f, 0.5f, 1.0f, -0.5f, 1.5f };
	for (size_t p = 0; p < ufbxt_arraycount(probe_weights); p++) {
		ufbx_real weights[2] = { probe_weights[p], probe_weights[p] * 0.5f };
		ufbxt_check_reference_displacement(err, mesh, deformer, weights);
	}

	static const double times[] = { 1.0/24.0, 16.0/24.0, 53.0/24.0, 120.0/24.0 };
	for (size_t f = 0; f < ufbxt_arraycount(times); f++) {
		ufbxt_check_animated_frame(err, scene, deformer, times[f]);
	}
}
#endif

// Blender-exported shapes carry per-control-point offset weights in
// addition to the channel driver weight. Each control point displacement
// must include that per-shape weight, while untouched points stay put.
UFBXT_FILE_TEST_ALT(blender440_shape_weight_anim_progressive, blender440_shape_weight_anim)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "Cube");
	if (!node) node = ufbx_find_node(scene, "Cube.001");
	ufbxt_assert(node && node->mesh);
	ufbx_mesh *mesh = node->mesh;
	ufbxt_assert(mesh->num_vertices == 8);
	ufbxt_assert(mesh->blend_deformers.count == 1);

	ufbx_blend_deformer *deformer = mesh->blend_deformers.data[0];
	ufbxt_assert(deformer->channels.count == 2);

	ufbx_blend_channel *key = ufbxt_find_blend_channel(deformer, "Key");
	ufbx_blend_channel *side = ufbxt_find_blend_channel(deformer, "Side");
	ufbxt_assert(key && side);

	for (size_t c = 0; c < deformer->channels.count; c++) {
		ufbx_blend_channel *channel = deformer->channels.data[c];
		ufbxt_assert(channel->keyframes.count == 1);
		ufbx_blend_shape *shape = channel->keyframes.data[0].shape;
		ufbxt_assert(shape->num_offsets == 4);
		ufbxt_assert(shape->offset_weights.count == 4);

		// Per-offset weights are source contract values in [0, 1].
		ufbx_real weight_sum = 0.0f;
		for (size_t i = 0; i < shape->offset_weights.count; i++) {
			ufbxt_assert(shape->offset_weights.data[i] >= 0.0f);
			ufbxt_assert(shape->offset_weights.data[i] <= 1.0f);
			weight_sum += shape->offset_weights.data[i];
		}
		ufbxt_assert(weight_sum > 0.0f);
	}

	// Static geometry check at arbitrary driver weights: the per-offset
	// weights must scale each control point displacement independently.
	static const ufbx_real driver_sets[][2] = {
		{ 0.0f, 0.0f },
		{ 0.5f, 0.25f },
		{ 1.0f, 1.0f },
	};
	for (size_t s = 0; s < ufbxt_arraycount(driver_sets); s++) {
		ufbxt_check_reference_displacement(err, mesh, deformer, driver_sets[s]);
	}

	// At the final animated frame both channels are fully driven.
	double time = 25.0 / 24.0;
	ufbx_real weights[2];
	weights[0] = ufbx_evaluate_blend_weight(scene->anim, key, time);
	weights[1] = ufbx_evaluate_blend_weight(scene->anim, side, time);
	ufbxt_assert(weights[0] > 0.0f && weights[1] > 0.0f);

	ufbx_evaluate_opts opts = { 0 };
	opts.evaluate_skinning = true;
	ufbx_scene *eval = ufbx_evaluate_scene(scene, scene->anim, time, &opts, NULL);
	ufbxt_assert(eval);

	ufbx_blend_deformer *eval_deformer = eval->blend_deformers.data[deformer->typed_id];
	ufbx_mesh *eval_mesh = ufbxt_find_deformer_mesh(eval, eval_deformer);
	ufbxt_assert(eval_mesh);

	ufbxt_check_progressive_contract(err, eval_deformer, weights);
	ufbxt_check_evaluated_geometry(err, eval_mesh, eval_deformer, weights);

	// Explicit per-vertex contract through the batch API: a zero per-offset
	// weight means the control point receives no displacement from that
	// shape even when the channel driver weight is non-zero.
	ufbx_blend_channel *eval_key = eval->blend_channels.data[key->typed_id];
	ufbx_blend_shape *key_shape = eval_key->keyframes.data[0].shape;
	for (size_t i = 0; i < key_shape->num_offsets; i++) {
		uint32_t vertex = key_shape->offset_vertices.data[i];
		ufbx_real per_vertex_weight = key_shape->offset_weights.data[i];
		ufbx_vec3 raw_offset = key_shape->position_offsets.data[i];

		ufbx_vec3 ref_key_part = ufbxt_mul3(ufbxt_mul3(raw_offset, per_vertex_weight), weights[0]);

		// Apply only the Key shape through the public batch API onto an
		// otherwise undeformed mesh.
		ufbx_vec3 key_only[UFBXT_BLEND_TEST_MAX_VERTICES];
		memcpy(key_only, eval_mesh->vertices.data, eval_mesh->num_vertices * sizeof(ufbx_vec3));
		ufbx_add_blend_shape_vertex_offsets(key_shape, key_only,
			eval_mesh->num_vertices, weights[0]);
		ufbx_vec3 key_part = ufbxt_sub3(key_only[vertex], eval_mesh->vertices.data[vertex]);
		ufbxt_assert_close_vec3(err, key_part, ref_key_part);

		// The raw shape query exposes the stored offset independently of the
		// per-offset control point weight.
		ufbx_vec3 raw_query = ufbx_get_blend_shape_vertex_offset(key_shape, vertex);
		ufbxt_assert_close_vec3(err, raw_query, raw_offset);
	}

	// Final skinned positions match the source-derived deformation and
	// unaffected control points remain exactly at their original positions.
	ufbx_vec3 ref_offsets[UFBXT_BLEND_TEST_MAX_VERTICES];
	ufbxt_ref_deformer_offsets(eval_deformer, weights, ref_offsets, eval_mesh->num_vertices);
	for (size_t v = 0; v < eval_mesh->num_vertices; v++) {
		ufbx_vec3 ref_position = ufbxt_add3(eval_mesh->vertex_position.values.data[v], ref_offsets[v]);
		ufbxt_assert_close_vec3(err, eval_mesh->skinned_position.values.data[v], ref_position);
		if (ref_offsets[v].x == 0.0f && ref_offsets[v].y == 0.0f && ref_offsets[v].z == 0.0f) {
			ufbxt_assert_vec3_equal(eval_mesh->skinned_position.values.data[v],
				eval_mesh->vertex_position.values.data[v].x,
				eval_mesh->vertex_position.values.data[v].y,
				eval_mesh->vertex_position.values.data[v].z);
		}
	}

	ufbxt_check_scene(eval);
	ufbx_free_scene(eval);
}
#endif

// Legacy Max blend deformer: valid single-target channels must expose the
// same geometric meaning through both the query and the batch APIs, with
// unaffected control points remaining in place.
UFBXT_FILE_TEST_ALT(max7_blend_cube_progressive, max7_blend_cube)
#if UFBXT_IMPL
{
	ufbx_node *node = ufbx_find_node(scene, "Box01");
	ufbxt_assert(node && node->mesh);
	ufbx_mesh *mesh = node->mesh;
	ufbxt_assert(mesh->blend_deformers.count == 1);

	ufbx_blend_deformer *deformer = mesh->blend_deformers.data[0];
	ufbxt_assert(deformer->channels.count == 2);

	for (size_t c = 0; c < deformer->channels.count; c++) {
		ufbx_blend_channel *channel = deformer->channels.data[c];
		ufbxt_assert(channel->keyframes.count == 1);
		ufbxt_assert_close_real(err, channel->keyframes.data[0].target_weight, 1.0f);
		ufbxt_assert(channel->target_shape == channel->keyframes.data[0].shape);
		ufbxt_assert(channel->keyframes.data[0].shape->num_offsets == 16);
	}

	static const ufbx_real driver_sets[][2] = {
		{ 0.0f, 0.0f },
		{ 0.3f, 0.7f },
		{ 1.0f, 1.0f },
	};
	for (size_t s = 0; s < ufbxt_arraycount(driver_sets); s++) {
		ufbxt_check_reference_displacement(err, mesh, deformer, driver_sets[s]);
	}
}
#endif
