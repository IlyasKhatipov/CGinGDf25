#include "rasterizer_renderer.h"

#include "utils/resource_utils.h"
#include "utils/timer.h"
#include <random>


void cg::renderer::rasterization_renderer::init()
{
	renderer::load_model();
	renderer::load_camera();
	rasterizer = std::make_shared<cg::renderer::rasterizer<cg::vertex, cg::unsigned_color>>();
	rasterizer->set_viewport(settings->width, settings->height);
	render_target = std::make_shared<cg::resource<cg::unsigned_color>>(settings->width, settings->height);
	depth_buffer = std::make_shared<cg::resource<float>>(settings->width, settings->height);
	rasterizer->set_render_target(render_target, depth_buffer);
}
void cg::renderer::rasterization_renderer::render()
{
	float4x4 matrix = mul(
		camera->get_projection_matrix(),
		camera->get_view_matrix(),
		model->get_world_matrix()
	);

	rasterizer->vertex_shader = [&](float4 vertex, cg::vertex vertex_data){
		auto processed = mul(matrix, vertex);
		return std::make_pair(processed, vertex_data);
	};

	rasterizer->pixel_shader = [](cg::vertex data, float z){
		return cg::color::from_float3(data.ambient);
	};

	rasterizer->clear_render_target(cg::unsigned_color{0, 0, 0});

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> dis(0.0f, 1.0f);

	linalg::aliases::float3 top_color{dis(gen), dis(gen), dis(gen)};
	linalg::aliases::float3 bottom_color{dis(gen), dis(gen), dis(gen)};

	for (unsigned y = 0; y < settings->height; y++) {
    	float t = float(y) / settings->height;
    	linalg::aliases::float3 color = top_color * (1.0f - t) + bottom_color * t;

    	cg::unsigned_color uc = cg::unsigned_color::from_float3(color);

    	for (unsigned x = 0; x < settings->width; x++) {
        	render_target->item(x, y) = uc;
    	}	
	}


	for(size_t shape_id = 0;shape_id < model->get_index_buffers().size();shape_id++)
	{
		rasterizer->set_vertex_buffer(model->get_vertex_buffers()[shape_id]);
		rasterizer->set_index_buffer(model->get_index_buffers()[shape_id]);
		rasterizer->draw(model->get_index_buffers()[shape_id]->count(), 0);
	}
	cg::utils::save_resource(*render_target, settings->result_path);
	
}

void cg::renderer::rasterization_renderer::destroy() {}

void cg::renderer::rasterization_renderer::update() {}