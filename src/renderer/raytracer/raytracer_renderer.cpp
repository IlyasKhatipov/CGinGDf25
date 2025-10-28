#include "raytracer_renderer.h"

#include "utils/resource_utils.h"
#include "utils/timer.h"
#include "utils/blue_noise.h"

#include <iostream>
#include "stb_image.h"

void cg::renderer::ray_tracing_renderer::init()
{
	renderer::load_model();
	renderer::load_camera();

	render_target = std::make_shared<cg::resource<unsigned_color>>(settings->width, settings->height);

	raytracer = std::make_shared<cg::renderer::raytracer<cg::vertex, cg::unsigned_color>>();

	raytracer->set_render_target(render_target);
	raytracer->set_viewport(settings->width, settings->height);
	raytracer->set_vertex_buffers(model->get_vertex_buffers());
	raytracer->set_index_buffers(model->get_index_buffers());
	raytracer->blue_noise_texture = std::make_shared<cg::utils::blue_noise>(
        "D:/projects/CGinGDf25/FreeBlueNoiseTextures/Data/1024_1024/LDR_RGBA_0.png"
    );

	raytracer->enable_denoising = true;
	raytracer->denoise_strength = 1.0f;

	lights.push_back({
		float3 {0.f, 1.58f, -0.03f},
		float3 {0.78f, 0.78f, 0.78f}
	});

	shadow_raytracer = std::make_shared<cg::renderer::raytracer<cg::vertex, cg::unsigned_color>>();

	shadow_raytracer->set_vertex_buffers(model->get_vertex_buffers());
	shadow_raytracer->set_index_buffers(model->get_index_buffers());	
}

void cg::renderer::ray_tracing_renderer::destroy() {}

void cg::renderer::ray_tracing_renderer::update() {}

void cg::renderer::ray_tracing_renderer::render()
{
	raytracer->clear_render_target({0, 0, 0});

	raytracer->miss_shader = [](const ray& ray){
		payload payload{};
		payload.color = {0.f, 0.f, 0.f};
		return payload;
	};

	{
		cg::utils::timer t("Ray generation");
		
		std::random_device random_device;
		std::mt19937 random_generator(random_device());
		std::uniform_real_distribution<float> uniform(-1.f, 1.f);
		raytracer->closest_hit_shader = [&](const ray& ray, payload& payload, const triangle<cg::vertex>& triangle, size_t depth){
			float3 position = ray.position + payload.t * ray.direction;
			float3 normal = normalize(
				payload.bary.x * triangle.na + 
				payload.bary.y * triangle.nb + 
				payload.bary.z * triangle.nc 
			);

			float3 result_color = triangle.emissive;
			float3 random_direction = {uniform(random_generator), uniform(random_generator), uniform(random_generator)};
			if (dot(normal, random_direction) < 0.f)
			{
				random_direction = -random_direction;
			}

			cg::renderer::ray to_next_object(position, random_direction);
			auto next_payload = raytracer->trace_ray(to_next_object, depth);
			result_color += triangle.diffuse * next_payload.color.to_float3() * 
					std::max(dot(normal, to_next_object.direction), 0.f);
			
			payload.color = cg::color::from_float3(result_color);
			return payload;
		};

		raytracer -> build_acceleration_structure();
		
		shadow_raytracer->miss_shader = [](const ray& ray){
			payload payload{};
			payload.t = -1.f;
			return payload;
		};

		shadow_raytracer->any_hit_shader = [](const ray& ray, payload& payload, const triangle<cg::vertex>& triangle){
			return payload;
		};

		shadow_raytracer->acceleration_structures = raytracer->acceleration_structures;

		raytracer->ray_generation(
			camera->get_position(), 
			camera->get_direction(),
			camera->get_right(), 
			camera->get_up(), 
			settings->raytracing_depth, 
			settings->accumulation_num
		);
	}

	cg::utils::save_resource(*render_target, settings->result_path);
}