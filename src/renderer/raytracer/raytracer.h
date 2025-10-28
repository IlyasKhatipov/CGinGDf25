#pragma once

#include "resource.h"
#include "utils/blue_noise.h"
#include <iostream>
#include <linalg.h>
#include <memory>
#include <omp.h>
#include <random>

using namespace linalg::aliases;

namespace cg::renderer
{
	struct ray
	{
		ray(float3 position, float3 direction) : position(position)
		{
			this->direction = normalize(direction);
		}
		float3 position;
		float3 direction;
	};

	struct payload
	{
		float t;
		float3 bary;
		cg::color color;
	};

	template<typename VB>
	struct triangle
	{
		triangle(const VB& vertex_a, const VB& vertex_b, const VB& vertex_c);

		float3 a;
		float3 b;
		float3 c;

		float3 ba;
		float3 ca;

		float3 na;
		float3 nb;
		float3 nc;

		float3 ambient;
		float3 diffuse;
		float3 emissive;
	};

	template<typename VB>
	inline triangle<VB>::triangle(
			const VB& vertex_a, const VB& vertex_b, const VB& vertex_c)
	{
		a = vertex_a.v;
		b = vertex_b.v;
		c = vertex_c.v;

		ba = b - a;
		ca = c - a;

		na = vertex_a.n;
		nb = vertex_b.n;
		nc = vertex_c.n;

		ambient = vertex_a.ambient;
		diffuse = vertex_a.diffuse;
		emissive = vertex_a.emissive;
	}

	template<typename VB>
	class aabb
	{
	public:
		void add_triangle(const triangle<VB> triangle);
		const std::vector<triangle<VB>>& get_triangles() const;
		bool aabb_test(const ray& ray) const;

	protected:
		std::vector<triangle<VB>> triangles;

		float3 aabb_min;
		float3 aabb_max;
	};

	struct light
	{
		float3 position;
		float3 color;
	};

	class Denoiser
	{
	public:
		void apply_bilateral_filter(cg::resource<float3>& image, 
								  size_t width, size_t height,
								  float sigma_space = 1.5f, 
								  float sigma_color = 0.1f, 
								  int radius = 2);
		
		void set_denoise_strength(float strength) { denoise_strength = strength; }
		
	private:
		float denoise_strength = 1.0f;
	};

	template<typename VB, typename RT>
	class raytracer
	{
	public:
		raytracer(){};
		~raytracer(){};

		void set_render_target(std::shared_ptr<resource<RT>> in_render_target);
		void clear_render_target(const RT& in_clear_value);
		void set_viewport(size_t in_width, size_t in_height);

		void set_vertex_buffers(std::vector<std::shared_ptr<cg::resource<VB>>> in_vertex_buffers);
		void set_index_buffers(std::vector<std::shared_ptr<cg::resource<unsigned int>>> in_index_buffers);
		void build_acceleration_structure();
		std::vector<aabb<VB>> acceleration_structures;

		void ray_generation(float3 position, float3 direction, float3 right, float3 up, size_t depth, size_t accumulation_num);
		
		Denoiser denoiser;
		bool enable_denoising = true;
		float denoise_strength = 0.5f;
		
		std::shared_ptr<cg::utils::blue_noise> blue_noise_texture;
		payload trace_ray(const ray& ray, size_t depth, float max_t = 1000.f, float min_t = 0.001f) const;
		payload intersection_shader(const triangle<VB>& triangle, const ray& ray) const;

		std::function<payload(const ray& ray)> miss_shader = nullptr;
		std::function<payload(const ray& ray, payload& payload, const triangle<VB>& triangle, size_t depth)>
				closest_hit_shader = nullptr;
		std::function<payload(const ray& ray, payload& payload, const triangle<VB>& triangle)> any_hit_shader =
				nullptr;

		float2 get_jitter(int frame_id);

	protected:
		std::shared_ptr<cg::resource<RT>> render_target;
		std::shared_ptr<cg::resource<float3>> history;
		std::vector<std::shared_ptr<cg::resource<unsigned int>>> index_buffers;
		std::vector<std::shared_ptr<cg::resource<VB>>> vertex_buffers;
		std::vector<triangle<VB>> triangles;

		size_t width = 1920;
		size_t height = 1080;
	};

	inline void Denoiser::apply_bilateral_filter(cg::resource<float3>& image, 
											   size_t width, size_t height,
											   float sigma_space, 
											   float sigma_color, 
											   int radius)
	{
		auto temp = std::make_shared<cg::resource<float3>>(width, height);
		
		const float sigma_space2 = sigma_space * sigma_space;
		const float sigma_color2 = sigma_color * sigma_color;
		
		#pragma omp parallel for
		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				float3 sum = float3(0.f);
				float total_weight = 0.f;
				float3 center_color = image.item(x, y);
				
				for (int dx = -radius; dx <= radius; dx++) {
					for (int dy = -radius; dy <= radius; dy++) {
						int nx = x + dx, ny = y + dy;
						if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
							float3 neighbor_color = image.item(nx, ny);
							
							float space_dist = dx*dx + dy*dy;
							float space_weight = exp(-space_dist / (2 * sigma_space2));
							
							float color_dist = length(neighbor_color - center_color);
							float color_weight = exp(-color_dist * color_dist / (2 * sigma_color2));
							
							float weight = space_weight * color_weight;
							sum += neighbor_color * weight;
							total_weight += weight;
						}
					}
				}
				
				if (total_weight > 0.f) {
					temp->item(x, y) = sum / total_weight;
				} else {
					temp->item(x, y) = center_color;
				}
			}
		}
		
		for (size_t i = 0; i < image.count(); i++) {
			image.item(i) = temp->item(i);
		}
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::set_render_target(
			std::shared_ptr<resource<RT>> in_render_target)
	{
		render_target = std::move(in_render_target);
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::set_viewport(size_t in_width,
												size_t in_height)
	{
		width = in_width;
		height = in_height;
		history = std::make_shared<cg::resource<float3>>(width, height);
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::clear_render_target(
			const RT& in_clear_value)
	{
		for (size_t i = 0;i<render_target->count();i++)
		{
			render_target->item(i) = in_clear_value;
			history->item(i) = float3(0.f);
		}
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::set_vertex_buffers(std::vector<std::shared_ptr<cg::resource<VB>>> in_vertex_buffers)
	{
		vertex_buffers = std::move(in_vertex_buffers);
	}

	template<typename VB, typename RT>
	void raytracer<VB, RT>::set_index_buffers(std::vector<std::shared_ptr<cg::resource<unsigned int>>> in_index_buffers)
	{
		index_buffers = std::move(in_index_buffers);
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::build_acceleration_structure()
	{
		for (size_t s=0;s<index_buffers.size();s++)
		{
			auto& index_buffer = index_buffers[s];
			auto& vertex_buffer = vertex_buffers[s];
			size_t i = 0;
			aabb<VB> aabb;
			while(i < index_buffer->count())
			{
				triangle <VB> triangle(
					vertex_buffer->item(index_buffer->item(i++)),
					vertex_buffer->item(index_buffer->item(i++)),
					vertex_buffer->item(index_buffer->item(i++))
				);
				aabb.add_triangle(triangle);
			}
			acceleration_structures.push_back(aabb);
		}
	}

	template<typename VB, typename RT>
	inline void raytracer<VB, RT>::ray_generation(
			float3 position, float3 direction,
			float3 right, float3 up, size_t depth, size_t accumulation_num)
	{
		float frame_weight = 1.f / accumulation_num;
		for (int frame_id = 0;frame_id < accumulation_num;frame_id++)
		{
			std::cout << "Tracing Frame #" << frame_id + 1 << "/" << accumulation_num << "\n";

			#pragma omp parallel for
			for (int x = 0; x < width; x++)
			{
				for (int y = 0; y < height; y++)
				{
					float3 noise = blue_noise_texture->get_value((x + frame_id) % width, y % height);
        			float2 jitter = float2(noise.x - 0.5f, noise.y - 0.5f);

        			float u = (2.f * x + jitter.x) / static_cast<float>(width - 1) - 1.f;
        			float v = (2.f * y + jitter.y) / static_cast<float>(height - 1) - 1.f;
        			u *= static_cast<float>(width) / static_cast<float>(height);
        			float3 ray_direction = direction + u * right - v * up;

        			ray ray(position, ray_direction);
        			payload payload = trace_ray(ray, depth);

        			auto& history_pixel = history->item(x, y);
        			history_pixel += sqrt(payload.color.to_float3() * frame_weight);
    			}
			}
		}

		if (enable_denoising && accumulation_num > 1) {
			std::cout << "Applying denoising..." << std::endl;
			
			float adaptive_strength = denoise_strength;
			if (accumulation_num >= 32) {
				adaptive_strength *= 0.3f;
			} else if (accumulation_num <= 8) {
				adaptive_strength *= 1.5f;
			}
			
			denoiser.set_denoise_strength(adaptive_strength);
			denoiser.apply_bilateral_filter(*history, width, height,
										  2.0f * adaptive_strength, 
										  0.2f * adaptive_strength, 
										  3);
		}

		for (int x = 0; x < width; x++) {
			for (int y = 0; y < height; y++) {
				cg::color color_pixel = cg::color::from_float3(history->item(x, y));
				render_target->item(x, y) = RT::from_color(color_pixel);
			}
		}
	}

	template<typename VB, typename RT>
	inline payload raytracer<VB, RT>::trace_ray(
			const ray& ray, size_t depth, float max_t, float min_t) const
	{
		if (depth == 0)
		{
			return miss_shader(ray);
		}
		depth--;
		
		payload closest_hit_payload{};
		closest_hit_payload.t = max_t;
		const triangle<VB>* closest_triangle = nullptr;

		for(auto& aabb : acceleration_structures)
		{
			if (!aabb.aabb_test(ray)){continue;}
			for (auto & triangle : aabb.get_triangles())
			{
				payload payload = intersection_shader(triangle, ray);
				if (payload.t > min_t && payload.t < closest_hit_payload.t)
				{
					if (any_hit_shader){return any_hit_shader(ray, payload, triangle);}
					closest_hit_payload = payload;
					closest_triangle = &triangle;
				}
			}
		}


		if (closest_hit_payload.t < max_t){
			if (closest_hit_shader){
				return closest_hit_shader(ray, closest_hit_payload, *closest_triangle, depth);
			}
		}
		return miss_shader(ray);
	}

	template<typename VB, typename RT>
	inline payload raytracer<VB, RT>::intersection_shader(
			const triangle<VB>& triangle, const ray& ray) const
	{
		payload payload{};
		payload.t = -1.f;

		float3 pvec = cross(ray.direction, triangle.ca);
		float det = dot(triangle.ba, pvec);
		if (det > -1e-8 && det < 1e-8){return payload;}

		float inv_det = 1.f / det;
		float3 tvec = ray.position - triangle.a;
		float u = dot(tvec, pvec) * inv_det;
		if (u < 0 || u > 1){return payload;}

		float3 qvec = cross(tvec, triangle.ba);
		float v = dot(ray.direction, qvec) * inv_det;
		if (v < 0.f || u + v > 1.f){return payload;}
		payload.t = dot(triangle.ca, qvec) * inv_det;
		payload.bary = float3(1.f - u - v, u, v);

		return payload;
	}

	template<typename VB, typename RT>
	float2 raytracer<VB, RT>::get_jitter(int frame_id)
	{
		float2 result(0.f);
		constexpr int base_x = 2;
		int index  = frame_id + 1;
		float inv_base = 1.f / base_x;
		float fraction = inv_base;
		while (index > 0)
		{
			result.x += (index % base_x) * fraction;
			index /= base_x;
			fraction *= inv_base;
		}

		constexpr int base_y = 3;
		index  = frame_id + 1;
		inv_base = 1.f / base_y;
		fraction = inv_base;
		while (index > 0)
		{
			result.y += (index % base_y) * fraction;
			index /= base_y;
			fraction *= inv_base;
		}
		return result - 0.5f;
	}


	template<typename VB>
	inline void aabb<VB>::add_triangle(const triangle<VB> triangle)
	{
		if (triangles.empty())
		{
			aabb_max = aabb_min = triangle.a;
		}

		triangles.push_back(triangle);
		
		aabb_max = max(aabb_max, triangle.a);
		aabb_max = max(aabb_max, triangle.b);
		aabb_max = max(aabb_max, triangle.c);

		aabb_min = min(aabb_min, triangle.a);
		aabb_min = min(aabb_min, triangle.b);
		aabb_min = min(aabb_min, triangle.c);
	}

	template<typename VB>
	inline const std::vector<triangle<VB>>& aabb<VB>::get_triangles() const
	{
		return triangles;
	}

	template<typename VB>
	inline bool aabb<VB>::aabb_test(const ray& ray) const
	{
		float3 invRaydir = float3(1.0) / ray.direction;
		float3 t0 = (aabb_max - ray.position) * invRaydir;
		float3 t1 = (aabb_min - ray.position) * invRaydir;
		float3 tmin = min(t0, t1);
		float3 tmax = max(t0, t1);
		return maxelem(tmin) <= minelem(tmax);
	}

}// namespace cg::renderer