#undef UFBXT_TEST_GROUP
#define UFBXT_TEST_GROUP "camera"

#if UFBXT_IMPL

static void ufbxt_check_ortho_camera(ufbxt_diff_error *err, ufbx_scene *scene, const char *name, ufbx_gate_fit gate_fit, ufbx_real extent, ufbx_real width, ufbx_real height)
{
	ufbxt_hintf("Cameara %s", name);
	ufbx_node *node = ufbx_find_node(scene, name);
	ufbxt_assert(node && node->camera);
	ufbx_camera *camera = node->camera;

	ufbxt_assert(camera->projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC);
	ufbxt_assert(camera->gate_fit == gate_fit);
	ufbxt_assert_close_real(err, camera->orthographic_extent, extent);
	ufbxt_assert_close_real(err, camera->orthographic_size.x, width);
	ufbxt_assert_close_real(err, camera->orthographic_size.y, height);
}

// -- Orthographic projection window derivation from source conventions --
//
// These helpers re-derive the orthographic projection window and the
// world-space frame from raw source properties and the evaluated node
// hierarchy, independently of the computed `ufbx_camera` fields. Tests using
// them catch implementations whose camera fields look superficially
// consistent yet crop the frame or let its scale drift.

static ufbx_vec2 ufbxt_derive_ortho_window(ufbx_real extent, ufbx_gate_fit gate_fit, ufbx_real film_aspect, ufbx_real view_aspect)
{
	ufbxt_assert(view_aspect > 0.0f);
	ufbxt_assert(film_aspect > 0.0f);

	ufbx_gate_fit fit = gate_fit;
	if (fit == UFBX_GATE_FIT_FILL) {
		fit = view_aspect > film_aspect ? UFBX_GATE_FIT_HORIZONTAL : UFBX_GATE_FIT_VERTICAL;
	} else if (fit == UFBX_GATE_FIT_OVERSCAN) {
		fit = view_aspect < film_aspect ? UFBX_GATE_FIT_HORIZONTAL : UFBX_GATE_FIT_VERTICAL;
	}

	ufbx_vec2 window;
	switch (fit) {
	case UFBX_GATE_FIT_HORIZONTAL:
		window.x = extent;
		window.y = extent / view_aspect;
		break;
	case UFBX_GATE_FIT_VERTICAL:
		window.x = extent * view_aspect;
		window.y = extent;
		break;
	default:
		// UFBX_GATE_FIT_NONE / UFBX_GATE_FIT_STRETCH use the extent directly
		window.x = extent;
		window.y = extent;
		break;
	}
	return window;
}

static ufbx_real ufbxt_camera_real_prop(ufbx_camera *camera, const char *name, ufbx_real fallback)
{
	ufbx_prop *prop = ufbx_find_prop(&camera->props, name);
	return prop != NULL ? (ufbx_real)prop->value_real : fallback;
}

// Derive the orthographic window (in camera-local units) straight from raw
// source properties (`OrthoZoom`, `AspectWidth/Height`, `FilmWidth/Height`,
// `FilmSqueezeRatio`) and the scene-level unit conventions.
static ufbx_vec2 ufbxt_derive_ortho_window_from_source(ufbx_scene *scene, ufbx_camera *camera)
{
	ufbx_real ortho_zoom = ufbxt_camera_real_prop(camera, "OrthoZoom", 1.0f);
	ufbx_real extent = scene->metadata.ortho_size_unit * ortho_zoom * scene->metadata.geometry_scale;

	ufbx_real aspect_x = ufbxt_camera_real_prop(camera, "AspectWidth", 0.0f);
	ufbx_real aspect_y = ufbxt_camera_real_prop(camera, "AspectHeight", 0.0f);
	ufbx_real film_x = ufbxt_camera_real_prop(camera, "FilmWidth", 0.0f);
	ufbx_real film_y = ufbxt_camera_real_prop(camera, "FilmHeight", 0.0f);
	ufbx_real squeeze = ufbxt_camera_real_prop(camera, "FilmSqueezeRatio", 1.0f);

	ufbx_real view_aspect = aspect_x > 0.0f && aspect_y > 0.0f ? aspect_x / aspect_y : 1.0f;
	film_y *= squeeze;
	ufbx_real film_aspect = film_x > 0.0f && film_y > 0.0f ? film_x / film_y : view_aspect;

	return ufbxt_derive_ortho_window(extent, camera->gate_fit, film_aspect, view_aspect);
}

static ufbx_vec3 ufbxt_coordinate_axis_vec(ufbx_coordinate_axis axis)
{
	ufbx_vec3 v = { 0.0f, 0.0f, 0.0f };
	switch (axis) {
	case UFBX_COORDINATE_AXIS_POSITIVE_X: v.x = 1.0f; break;
	case UFBX_COORDINATE_AXIS_NEGATIVE_X: v.x = -1.0f; break;
	case UFBX_COORDINATE_AXIS_POSITIVE_Y: v.y = 1.0f; break;
	case UFBX_COORDINATE_AXIS_NEGATIVE_Y: v.y = -1.0f; break;
	case UFBX_COORDINATE_AXIS_POSITIVE_Z: v.z = 1.0f; break;
	case UFBX_COORDINATE_AXIS_NEGATIVE_Z: v.z = -1.0f; break;
	default: ufbxt_assert(false); break;
	}
	return v;
}

static ufbx_real ufbxt_dot_vec3(ufbx_vec3 a, ufbx_vec3 b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

// Project a world-space point through an orthographic camera using the
// evaluated node hierarchy. Returns { ndc_x, ndc_y, view_depth } where the
// NDC spans [-1,1] over the frame and view_depth is positive in front of
// the camera.
static ufbx_vec3 ufbxt_project_ortho(ufbx_camera *camera, ufbx_node *camera_node, ufbx_vec3 world_pos)
{
	ufbx_matrix world_to_camera = ufbx_matrix_invert(&camera_node->node_to_world);
	ufbx_vec3 local = ufbx_transform_position(&world_to_camera, world_pos);

	ufbx_vec3 proj;
	proj.x = ufbxt_dot_vec3(local, ufbxt_coordinate_axis_vec(camera->projection_axes.right)) / (0.5f * camera->orthographic_size.x);
	proj.y = ufbxt_dot_vec3(local, ufbxt_coordinate_axis_vec(camera->projection_axes.up)) / (0.5f * camera->orthographic_size.y);
	proj.z = -ufbxt_dot_vec3(local, ufbxt_coordinate_axis_vec(camera->projection_axes.front));
	return proj;
}

static ufbx_real ufbxt_matrix_axis_scale(const ufbx_matrix *m, ufbx_coordinate_axis axis)
{
	uint32_t ix = (uint32_t)axis / 2;
	ufbxt_assert(ix < 3);
	ufbx_vec3 col = m->cols[ix];
	return (ufbx_real)sqrt((double)ufbxt_dot_vec3(col, col));
}

// World-space size of the projection window: the camera-local window composed
// with the evaluated hierarchy scale of the camera node.
static ufbx_vec2 ufbxt_ortho_world_window(ufbx_camera *camera, ufbx_node *camera_node)
{
	ufbx_vec2 window;
	window.x = camera->orthographic_size.x * ufbxt_matrix_axis_scale(&camera_node->node_to_world, camera->projection_axes.right);
	window.y = camera->orthographic_size.y * ufbxt_matrix_axis_scale(&camera_node->node_to_world, camera->projection_axes.up);
	return window;
}

#endif

UFBXT_FILE_TEST(maya_ortho_camera_400x200)
#if UFBXT_IMPL
{
	ufbxt_check_ortho_camera(err, scene, "Fill", UFBX_GATE_FIT_FILL, 30.0f, 30.0f, 15.0f);
	ufbxt_check_ortho_camera(err, scene, "Horizontal", UFBX_GATE_FIT_HORIZONTAL, 30.0f, 30.0f, 15.0f);
	ufbxt_check_ortho_camera(err, scene, "Vertical", UFBX_GATE_FIT_VERTICAL, 30.0f, 60.0f, 30.0f);
	ufbxt_check_ortho_camera(err, scene, "Overscan", UFBX_GATE_FIT_OVERSCAN, 30.0f, 60.0f, 30.0f);
}
#endif

UFBXT_FILE_TEST(maya_ortho_camera_200x300)
#if UFBXT_IMPL
{
	ufbxt_check_ortho_camera(err, scene, "Fill", UFBX_GATE_FIT_FILL, 30.0f, 20.0f, 30.0f);
	ufbxt_check_ortho_camera(err, scene, "Horizontal", UFBX_GATE_FIT_HORIZONTAL, 30.0f, 30.0f, 45.0f);
	ufbxt_check_ortho_camera(err, scene, "Vertical", UFBX_GATE_FIT_VERTICAL, 30.0f, 20.0f, 30.0f);
	ufbxt_check_ortho_camera(err, scene, "Overscan", UFBX_GATE_FIT_OVERSCAN, 30.0f, 30.0f, 45.0f);
}
#endif

UFBXT_FILE_TEST(maya_ortho_camera_size)
#if UFBXT_IMPL
{
	ufbxt_check_ortho_camera(err, scene, "Ortho_10", UFBX_GATE_FIT_FILL, 10.0f, 10.0f, 10.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_30", UFBX_GATE_FIT_FILL, 30.0f, 30.0f, 30.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_35", UFBX_GATE_FIT_FILL, 35.0f, 35.0f, 35.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_100", UFBX_GATE_FIT_FILL, 100.0f, 100.0f, 100.0f);
}
#endif

UFBXT_FILE_TEST(blender_402_ortho_scale_local)
#if UFBXT_IMPL
{
	ufbxt_check_ortho_camera(err, scene, "Ortho_1", UFBX_GATE_FIT_HORIZONTAL, 1.0f, 1.0f, 1.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_2", UFBX_GATE_FIT_HORIZONTAL, 2.0f, 2.0f, 2.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_3", UFBX_GATE_FIT_HORIZONTAL, 3.0f, 3.0f, 3.0f);
}
#endif

UFBXT_TEST(blender_402_ortho_scale_local_conversion)
#if UFBXT_IMPL
{
	ufbxt_diff_error err = { 0 };

	char path[512];
	ufbxt_file_iterator iter = { "blender_402_ortho_scale_local" };
	while (ufbxt_next_file(&iter, path, sizeof(path))) {
		for (uint32_t i = 0; i < UFBX_SPACE_CONVERSION_COUNT; i++) {
			ufbx_load_opts opts = { 0 };
			opts.space_conversion = i;
			opts.target_unit_meters = 1.0f;

			ufbx_scene *scene = ufbx_load_file(path, &opts, NULL);
			ufbxt_assert(scene);
			ufbxt_check_ortho_camera(&err, scene, "Ortho_1", UFBX_GATE_FIT_HORIZONTAL, 1.0f, 1.0f, 1.0f);
			ufbxt_check_ortho_camera(&err, scene, "Ortho_2", UFBX_GATE_FIT_HORIZONTAL, 2.0f, 2.0f, 2.0f);
			ufbxt_check_ortho_camera(&err, scene, "Ortho_3", UFBX_GATE_FIT_HORIZONTAL, 3.0f, 3.0f, 3.0f);
			ufbx_free_scene(scene);
		}
	}

	ufbxt_logf(".. Absolute diff: avg %.3g, max %.3g (%zu tests)", err.sum / (ufbx_real)err.num, err.max, err.num);
}
#endif

UFBXT_FILE_TEST(blender_402_ortho_scale_unit)
#if UFBXT_IMPL
{
	ufbxt_check_ortho_camera(err, scene, "Ortho_1", UFBX_GATE_FIT_HORIZONTAL, 1.0f, 1.0f, 1.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_2", UFBX_GATE_FIT_HORIZONTAL, 2.0f, 2.0f, 2.0f);
	ufbxt_check_ortho_camera(err, scene, "Ortho_3", UFBX_GATE_FIT_HORIZONTAL, 3.0f, 3.0f, 3.0f);
}
#endif

UFBXT_TEST(blender_402_ortho_scale_unit_conversion)
#if UFBXT_IMPL
{
	ufbxt_diff_error err = { 0 };

	char path[512];
	ufbxt_file_iterator iter = { "blender_402_ortho_scale_unit" };
	while (ufbxt_next_file(&iter, path, sizeof(path))) {
		for (uint32_t i = 0; i < UFBX_SPACE_CONVERSION_COUNT; i++) {
			ufbx_load_opts opts = { 0 };
			opts.space_conversion = i;
			opts.target_unit_meters = 1.0f;

			ufbx_scene *scene = ufbx_load_file(path, &opts, NULL);
			ufbxt_assert(scene);
			ufbxt_check_ortho_camera(&err, scene, "Ortho_1", UFBX_GATE_FIT_HORIZONTAL, 1.0f, 1.0f, 1.0f);
			ufbxt_check_ortho_camera(&err, scene, "Ortho_2", UFBX_GATE_FIT_HORIZONTAL, 2.0f, 2.0f, 2.0f);
			ufbxt_check_ortho_camera(&err, scene, "Ortho_3", UFBX_GATE_FIT_HORIZONTAL, 3.0f, 3.0f, 3.0f);
			ufbx_free_scene(scene);
		}
	}

	ufbxt_logf(".. Absolute diff: avg %.3g, max %.3g (%zu tests)", err.sum / (ufbx_real)err.num, err.max, err.num);
}
#endif

UFBXT_TEST(ortho_camera_window_from_source_conventions)
#if UFBXT_IMPL
{
	// The computed projection window must be reproducible from the raw source
	// properties (OrthoZoom / AspectWidth / AspectHeight / FilmWidth / FilmHeight)
	// and the scene unit conventions alone, across exporters and gate fit modes.
	ufbxt_diff_error err = { 0 };

	const char *files[] = {
		"maya_ortho_camera_400x200",
		"maya_ortho_camera_200x300",
		"maya_ortho_camera_size",
		"blender_402_ortho_scale_local",
		"blender_402_ortho_scale_unit",
	};

	for (size_t file_ix = 0; file_ix < ufbxt_arraycount(files); file_ix++) {
		char path[512];
		ufbxt_file_iterator iter = { files[file_ix] };
		while (ufbxt_next_file(&iter, path, sizeof(path))) {
			ufbx_scene *scene = ufbx_load_file(path, NULL, NULL);
			ufbxt_assert(scene);

			for (size_t ci = 0; ci < scene->cameras.count; ci++) {
				ufbx_camera *camera = scene->cameras.data[ci];
				if (camera->projection_mode != UFBX_PROJECTION_MODE_ORTHOGRAPHIC) continue;

				ufbxt_hintf("file=%s camera=%s", path, camera->name.data);

				ufbx_vec2 window = ufbxt_derive_ortho_window_from_source(scene, camera);
				ufbxt_assert_close_vec2(&err, camera->orthographic_size, window);

				// For orthographic cameras the projection plane at distance 1
				// is exactly the projection window.
				ufbxt_assert_close_vec2(&err, camera->projection_plane, window);

				// The frame aspect must agree with the render resolution.
				ufbxt_assert(camera->resolution.y > 0.0f);
				ufbxt_assert_close_real(&err, camera->aspect_ratio, camera->resolution.x / camera->resolution.y);
			}

			ufbx_free_scene(scene);
		}
	}

	ufbxt_logf(".. Absolute diff: avg %.3g, max %.3g (%zu tests)", err.sum / (ufbx_real)err.num, err.max, err.num);
}
#endif

UFBXT_TEST(ortho_camera_gate_fit_across_viewports)
#if UFBXT_IMPL
{
	// maya_ortho_camera_400x200 and maya_ortho_camera_200x300 contain the same
	// four cameras (extent 30, same film gate) framed at viewport aspects 2.0
	// and 0.667. The imported windows of the same camera at different viewport
	// aspects must relate exactly as the source gate-fit conventions dictate:
	// the fit axis must not drift, and Fill/Overscan must select the
	// cropping/padding fit respectively.
	ufbxt_diff_error err = { 0 };

	const char *camera_names[] = { "Fill", "Horizontal", "Vertical", "Overscan" };

	char wide_path[512], tall_path[512];
	ufbxt_file_iterator wide_iter = { "maya_ortho_camera_400x200" };
	while (ufbxt_next_file(&wide_iter, wide_path, sizeof(wide_path))) {
		ufbxt_file_iterator tall_iter = { "maya_ortho_camera_200x300" };
		while (ufbxt_next_file(&tall_iter, tall_path, sizeof(tall_path))) {
			ufbx_scene *wide_scene = ufbx_load_file(wide_path, NULL, NULL);
			ufbx_scene *tall_scene = ufbx_load_file(tall_path, NULL, NULL);
			ufbxt_assert(wide_scene && tall_scene);

			for (size_t ci = 0; ci < ufbxt_arraycount(camera_names); ci++) {
				const char *name = camera_names[ci];
				ufbxt_hintf("camera=%s wide=%s tall=%s", name, wide_path, tall_path);

				ufbx_node *wide_node = ufbx_find_node(wide_scene, name);
				ufbx_node *tall_node = ufbx_find_node(tall_scene, name);
				ufbxt_assert(wide_node && wide_node->camera);
				ufbxt_assert(tall_node && tall_node->camera);
				ufbx_camera *wide = wide_node->camera;
				ufbx_camera *tall = tall_node->camera;

				// Same source camera, only the viewport aspect differs.
				ufbxt_assert(wide->gate_fit == tall->gate_fit);
				ufbxt_assert_close_real(&err, wide->orthographic_extent, tall->orthographic_extent);
				ufbx_real extent = wide->orthographic_extent;

				// Each window must match the source-convention derivation at
				// its own viewport aspect.
				ufbxt_assert_close_vec2(&err, wide->orthographic_size, ufbxt_derive_ortho_window_from_source(wide_scene, wide));
				ufbxt_assert_close_vec2(&err, tall->orthographic_size, ufbxt_derive_ortho_window_from_source(tall_scene, tall));

				ufbx_real wide_area = wide->orthographic_size.x * wide->orthographic_size.y;
				ufbx_real tall_area = tall->orthographic_size.x * tall->orthographic_size.y;

				switch (wide->gate_fit) {
				case UFBX_GATE_FIT_HORIZONTAL:
					// Width is the fit axis: stable across viewport aspects.
					ufbxt_assert_close_real(&err, wide->orthographic_size.x, extent);
					ufbxt_assert_close_real(&err, tall->orthographic_size.x, extent);
					// Visible height must follow the viewport height exactly.
					ufbxt_assert_close_real(&err, wide->orthographic_size.y * wide->aspect_ratio, extent);
					ufbxt_assert_close_real(&err, tall->orthographic_size.y * tall->aspect_ratio, extent);
					ufbxt_assert(tall->orthographic_size.y > wide->orthographic_size.y);
					break;
				case UFBX_GATE_FIT_VERTICAL:
					// Height is the fit axis: stable across viewport aspects.
					ufbxt_assert_close_real(&err, wide->orthographic_size.y, extent);
					ufbxt_assert_close_real(&err, tall->orthographic_size.y, extent);
					// Visible width must follow the viewport width exactly.
					ufbxt_assert_close_real(&err, wide->orthographic_size.x / wide->aspect_ratio, extent);
					ufbxt_assert_close_real(&err, tall->orthographic_size.x / tall->aspect_ratio, extent);
					ufbxt_assert(wide->orthographic_size.x > tall->orthographic_size.x);
					break;
				case UFBX_GATE_FIT_FILL:
					// Fill crops: never shows more than the extent square.
					ufbxt_assert(wide_area <= extent * extent * 1.001f);
					ufbxt_assert(tall_area <= extent * extent * 1.001f);
					break;
				case UFBX_GATE_FIT_OVERSCAN:
					// Overscan pads: never shows less than the extent square.
					ufbxt_assert(wide_area >= extent * extent * 0.999f);
					ufbxt_assert(tall_area >= extent * extent * 0.999f);
					break;
				default:
					ufbxt_assert(false);
					break;
				}
			}

			// Fill/Overscan must agree with the explicit axis fits: Fill
			// selects the cropping one, Overscan the padding one, and the
			// choice flips with the viewport aspect.
			ufbx_camera *wide_fill = ufbx_find_node(wide_scene, "Fill")->camera;
			ufbx_camera *tall_fill = ufbx_find_node(tall_scene, "Fill")->camera;
			ufbx_camera *wide_overscan = ufbx_find_node(wide_scene, "Overscan")->camera;
			ufbx_camera *tall_overscan = ufbx_find_node(tall_scene, "Overscan")->camera;
			ufbx_camera *wide_horizontal = ufbx_find_node(wide_scene, "Horizontal")->camera;
			ufbx_camera *tall_horizontal = ufbx_find_node(tall_scene, "Horizontal")->camera;
			ufbx_camera *wide_vertical = ufbx_find_node(wide_scene, "Vertical")->camera;
			ufbx_camera *tall_vertical = ufbx_find_node(tall_scene, "Vertical")->camera;

			ufbxt_assert_close_vec2(&err, wide_fill->orthographic_size, wide_horizontal->orthographic_size);
			ufbxt_assert_close_vec2(&err, tall_fill->orthographic_size, tall_vertical->orthographic_size);
			ufbxt_assert_close_vec2(&err, wide_overscan->orthographic_size, wide_vertical->orthographic_size);
			ufbxt_assert_close_vec2(&err, tall_overscan->orthographic_size, tall_horizontal->orthographic_size);

			ufbx_free_scene(wide_scene);
			ufbx_free_scene(tall_scene);
		}
	}

	ufbxt_logf(".. Absolute diff: avg %.3g, max %.3g (%zu tests)", err.sum / (ufbx_real)err.num, err.max, err.num);
}
#endif

UFBXT_TEST(ortho_camera_projection_unit_hierarchy_invariant)
#if UFBXT_IMPL
{
	// Projecting the same object through the evaluated camera hierarchy must
	// produce identical frame coordinates regardless of how scene units are
	// converted, and the frame must match the window derived independently
	// from the source camera conventions composed with the hierarchy scale.
	ufbxt_diff_error err = { 0 };

	const char *camera_names[] = { "Fill", "Horizontal", "Vertical", "Overscan" };

	// The cube pCube1 is 30 source units wide and centered on the view axis,
	// the ortho extent is 30: with a horizontal fit it exactly fills the frame
	// width and is cropped to twice the frame height, with a vertical fit it
	// fills the frame height and half the frame width.
	const ufbx_real ref_ndc_x[] = { 1.0f, 1.0f, 0.5f, 0.5f };
	const ufbx_real ref_ndc_y[] = { 2.0f, 2.0f, 1.0f, 1.0f };

	char path[512];
	ufbxt_file_iterator iter = { "maya_ortho_camera_400x200" };
	while (ufbxt_next_file(&iter, path, sizeof(path))) {
		ufbx_vec2 ref_min[4] = { 0 };
		ufbx_vec2 ref_max[4] = { 0 };
		bool has_ref = false;

		for (uint32_t variant = 0; variant < 3; variant++) {
			ufbx_load_opts opts = { 0 };
			opts.target_camera_axes = ufbx_axes_right_handed_y_up;
			if (variant >= 1) opts.target_unit_meters = 1.0f;
			if (variant >= 2) opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

			ufbxt_hintf("file=%s variant=%u", path, variant);

			ufbx_scene *scene = ufbx_load_file(path, &opts, NULL);
			ufbxt_assert(scene);

			ufbx_node *cube = ufbx_find_node(scene, "pCube1");
			ufbxt_assert(cube && cube->mesh);

			for (size_t ci = 0; ci < ufbxt_arraycount(camera_names); ci++) {
				ufbx_node *cam_node = ufbx_find_node(scene, camera_names[ci]);
				ufbxt_assert(cam_node && cam_node->camera);
				ufbx_camera *camera = cam_node->camera;

				// Project every cube vertex through the evaluated hierarchy.
				ufbx_vec2 ndc_min = { +1e30f, +1e30f };
				ufbx_vec2 ndc_max = { -1e30f, -1e30f };
				for (size_t vi = 0; vi < cube->mesh->vertices.count; vi++) {
					ufbx_vec3 world = ufbx_transform_position(&cube->geometry_to_world, cube->mesh->vertices.data[vi]);
					ufbx_vec3 proj = ufbxt_project_ortho(camera, cam_node, world);
					ufbxt_assert(proj.z > 0.0f);
					if (proj.x < ndc_min.x) ndc_min.x = proj.x;
					if (proj.y < ndc_min.y) ndc_min.y = proj.y;
					if (proj.x > ndc_max.x) ndc_max.x = proj.x;
					if (proj.y > ndc_max.y) ndc_max.y = proj.y;
				}

				// The frame is symmetric around the view axis.
				ufbxt_assert_close_real(&err, ndc_min.x, -ndc_max.x);
				ufbxt_assert_close_real(&err, ndc_min.y, -ndc_max.y);

				// Absolute anchors: the visible region and image scale of the
				// cube are fully determined by the source conventions.
				ufbxt_assert_close_real(&err, ndc_max.x, ref_ndc_x[ci]);
				ufbxt_assert_close_real(&err, ndc_max.y, ref_ndc_y[ci]);

				// The projected frame must match the source-convention window
				// composed with the evaluated hierarchy scale: the cube spans
				// 30 source units, so the NDC bound is 15 / (window/2).
				ufbx_vec2 derived = ufbxt_derive_ortho_window_from_source(scene, camera);
				ufbx_real cam_scale_x = ufbxt_matrix_axis_scale(&cam_node->node_to_world, camera->projection_axes.right);
				ufbx_real cam_scale_y = ufbxt_matrix_axis_scale(&cam_node->node_to_world, camera->projection_axes.up);
				ufbx_vec3 cube_world = ufbx_transform_position(&cube->geometry_to_world, cube->mesh->vertices.data[0]);
				ufbx_real cube_half = (ufbx_real)fabs((double)cube_world.x);
				ufbxt_assert_close_real(&err, ndc_max.x, cube_half / (0.5f * derived.x * cam_scale_x));
				ufbxt_assert_close_real(&err, ndc_max.y, cube_half / (0.5f * derived.y * cam_scale_y));

				// The frame content must not drift between unit conversions.
				if (has_ref) {
					ufbxt_assert_close_vec2(&err, ndc_min, ref_min[ci]);
					ufbxt_assert_close_vec2(&err, ndc_max, ref_max[ci]);
				} else {
					ref_min[ci] = ndc_min;
					ref_max[ci] = ndc_max;
				}
			}

			// Scene evaluation must reproduce the identical frame.
			ufbx_scene *eval_scene = ufbx_evaluate_scene(scene, NULL, 0.0, NULL, NULL);
			ufbxt_assert(eval_scene);

			ufbx_node *eval_cube = ufbx_find_node(eval_scene, "pCube1");
			ufbxt_assert(eval_cube && eval_cube->mesh);

			for (size_t ci = 0; ci < ufbxt_arraycount(camera_names); ci++) {
				ufbx_node *eval_cam_node = ufbx_find_node(eval_scene, camera_names[ci]);
				ufbxt_assert(eval_cam_node && eval_cam_node->camera);

				ufbx_vec2 ndc_min = { +1e30f, +1e30f };
				ufbx_vec2 ndc_max = { -1e30f, -1e30f };
				for (size_t vi = 0; vi < eval_cube->mesh->vertices.count; vi++) {
					ufbx_vec3 world = ufbx_transform_position(&eval_cube->geometry_to_world, eval_cube->mesh->vertices.data[vi]);
					ufbx_vec3 proj = ufbxt_project_ortho(eval_cam_node->camera, eval_cam_node, world);
					ufbxt_assert(proj.z > 0.0f);
					if (proj.x < ndc_min.x) ndc_min.x = proj.x;
					if (proj.y < ndc_min.y) ndc_min.y = proj.y;
					if (proj.x > ndc_max.x) ndc_max.x = proj.x;
					if (proj.y > ndc_max.y) ndc_max.y = proj.y;
				}

				ufbxt_assert_close_real(&err, ndc_max.x, ref_ndc_x[ci]);
				ufbxt_assert_close_real(&err, ndc_max.y, ref_ndc_y[ci]);
			}

			ufbx_free_scene(eval_scene);
			ufbx_free_scene(scene);
			has_ref = true;
		}
	}

	ufbxt_logf(".. Absolute diff: avg %.3g, max %.3g (%zu tests)", err.sum / (ufbx_real)err.num, err.max, err.num);
}
#endif

UFBXT_TEST(ortho_camera_scale_convention_frame_equivalence)
#if UFBXT_IMPL
{
	// blender_402_ortho_scale_local and blender_402_ortho_scale_unit encode the
	// same ortho cameras through different source conventions (camera object
	// scale vs. unit scale). The camera fields alone look identical, but only
	// the composed world-space frame reveals whether the projection window
	// survived: both files must frame world-space content identically.
	ufbxt_diff_error err = { 0 };

	const char *camera_names[] = { "Ortho_1", "Ortho_2", "Ortho_3" };
	const ufbx_real probe_dx[] = { -0.4f, 0.0f, 0.35f };
	const ufbx_real probe_dy[] = { -0.3f, 0.25f };

	char local_path[512], unit_path[512];
	ufbxt_file_iterator local_iter = { "blender_402_ortho_scale_local" };
	while (ufbxt_next_file(&local_iter, local_path, sizeof(local_path))) {
		ufbxt_file_iterator unit_iter = { "blender_402_ortho_scale_unit" };
		while (ufbxt_next_file(&unit_iter, unit_path, sizeof(unit_path))) {
			ufbx_load_opts opts = { 0 };
			opts.target_camera_axes = ufbx_axes_right_handed_y_up;
			opts.target_unit_meters = 1.0f;

			ufbxt_hintf("local=%s unit=%s", local_path, unit_path);

			ufbx_scene *local_scene = ufbx_load_file(local_path, &opts, NULL);
			ufbx_scene *unit_scene = ufbx_load_file(unit_path, &opts, NULL);
			ufbxt_assert(local_scene && unit_scene);

			for (size_t ci = 0; ci < ufbxt_arraycount(camera_names); ci++) {
				ufbx_node *local_node = ufbx_find_node(local_scene, camera_names[ci]);
				ufbx_node *unit_node = ufbx_find_node(unit_scene, camera_names[ci]);
				ufbxt_assert(local_node && local_node->camera);
				ufbxt_assert(unit_node && unit_node->camera);
				ufbx_camera *local_camera = local_node->camera;
				ufbx_camera *unit_camera = unit_node->camera;

				// The evaluated camera positions must agree.
				ufbxt_assert_close_vec3(&err, local_node->node_to_world.cols[3], unit_node->node_to_world.cols[3]);

				// The world-space projection window must agree even though the
				// camera-local fields and node scales differ per convention.
				ufbx_vec2 local_window = ufbxt_ortho_world_window(local_camera, local_node);
				ufbx_vec2 unit_window = ufbxt_ortho_world_window(unit_camera, unit_node);
				ufbxt_assert_close_vec2(&err, local_window, unit_window);

				// The window of Ortho_N is N meters square in world space.
				ufbxt_assert_close_real(&err, local_window.x, (ufbx_real)(ci + 1));
				ufbxt_assert_close_real(&err, local_window.y, (ufbx_real)(ci + 1));

				ufbx_vec3 cam_pos = local_node->node_to_world.cols[3];
				for (size_t xi = 0; xi < ufbxt_arraycount(probe_dx); xi++) {
					for (size_t yi = 0; yi < ufbxt_arraycount(probe_dy); yi++) {
						ufbx_vec3 p = { cam_pos.x + probe_dx[xi], cam_pos.y + probe_dy[yi], cam_pos.z - 2.0f };

						ufbx_vec3 local_proj = ufbxt_project_ortho(local_camera, local_node, p);
						ufbx_vec3 unit_proj = ufbxt_project_ortho(unit_camera, unit_node, p);

						// Same object, same frame, independent of the source convention.
						ufbxt_assert_close_vec3(&err, local_proj, unit_proj);

						// The probe is 2 meters in front of the camera.
						ufbxt_assert_close_real(&err, local_proj.z, 2.0f);

						// Absolute anchor: NDC within the N-meter square window.
						ufbx_real half_window = 0.5f * (ufbx_real)(ci + 1);
						ufbxt_assert_close_real(&err, local_proj.x, probe_dx[xi] / half_window);
						ufbxt_assert_close_real(&err, local_proj.y, probe_dy[yi] / half_window);
					}
				}
			}

			ufbx_free_scene(local_scene);
			ufbx_free_scene(unit_scene);
		}
	}

	ufbxt_logf(".. Absolute diff: avg %.3g, max %.3g (%zu tests)", err.sum / (ufbx_real)err.num, err.max, err.num);
}
#endif


