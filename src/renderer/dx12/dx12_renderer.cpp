#include "dx12_renderer.h"

#include "utils/com_error_handler.h"
#include "utils/window.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>


void cg::renderer::dx12_renderer::init()
{
	cg::renderer::renderer::load_camera();
	cg::renderer::renderer::load_model();

	shadow_light = std::make_shared<cg::world::camera>();
	shadow_light->set_height(static_cast<float>(settings->height));
	shadow_light->set_width(static_cast<float>(settings->width));
	shadow_light->set_position(float3{
		settings->camera_position[0],
		settings->camera_position[1] + 20.f,
		settings->camera_position[2] - 5.f
	});
	shadow_light->set_theta(0.f);
	shadow_light->set_phi(-90.f);
	shadow_light->set_angle_of_view(settings->camera_angle_of_view);
	shadow_light->set_z_near(settings->camera_z_near);
	shadow_light->set_z_far(settings->camera_z_far);

	cb.light.color = float4{1.0f, 0.f, 0.f, 1.0f};
	cb.light.position = float4{shadow_light->get_position(), 1.f};
	cb.mwpMatrix = camera->get_dxm_mvp_matrix();
	cb.shadowMatrix = shadow_light->get_dxm_mvp_matrix();

	view_port = CD3DX12_VIEWPORT(0.f, 0.f, static_cast<float>(settings->width), static_cast<float>(settings->height));
	scissor_rect = CD3DX12_RECT(0, 0, static_cast<LONG>(settings->width), static_cast<LONG>(settings->height));
	load_pipeline();
	load_assets();
}

void cg::renderer::dx12_renderer::destroy()
{
	wait_for_gpu();
	CloseHandle(fence_event);
}

void cg::renderer::dx12_renderer::update()
{
	auto now = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float> durarion = now - current_time;
	frame_duration = durarion.count();
	current_time = now;

	cb.mwpMatrix = camera->get_dxm_mvp_matrix();
	memcpy(constant_buffer_data_begin, &cb, sizeof(cb));
}

void cg::renderer::dx12_renderer::render()
{
	populate_command_list();

	ID3D12CommandList* cls[] = {command_list.Get()};
	command_queue->ExecuteCommandLists(_countof(cls), cls);

	THROW_IF_FAILED(swap_chain->Present(0, 0));
	move_to_next_frame();
}

ComPtr<IDXGIFactory4> cg::renderer::dx12_renderer::get_dxgi_factory()
{
	UINT dxgi_factory_flags = 0;
	#ifdef _DEBUG
		ComPtr<ID3D12Debug> debug_controller;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
		{
			debug_controller->EnableDebugLayer();
			dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	#endif

	ComPtr<IDXGIFactory4> dxgi_factory;
	THROW_IF_FAILED(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory)));
	return dxgi_factory;
}

void cg::renderer::dx12_renderer::initialize_device(ComPtr<IDXGIFactory4>& dxgi_factory)
{
	ComPtr<IDXGIAdapter1> hardware_adapter;
	THROW_IF_FAILED(dxgi_factory->EnumAdapters1(0, &hardware_adapter));
	#ifdef _DEBUG
		DXGI_ADAPTER_DESC desc{};
		hardware_adapter->GetDesc(&desc);
		OutputDebugString(desc.Description);
		OutputDebugString(L"\n");
	#endif
	THROW_IF_FAILED(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
}

void cg::renderer::dx12_renderer::create_direct_command_queue()
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	THROW_IF_FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue)));
}

void cg::renderer::dx12_renderer::create_swap_chain(ComPtr<IDXGIFactory4>& dxgi_factory)
{
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.BufferCount = frame_number;
	desc.Height = settings->height;
	desc.Width = settings->width;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.SampleDesc.Count = 1;
	ComPtr<IDXGISwapChain1> temp_swap_chain;
	THROW_IF_FAILED(dxgi_factory->CreateSwapChainForHwnd(
		command_queue.Get(),cg::utils::window::get_hwnd(),&desc, nullptr, nullptr, &temp_swap_chain
	));
	dxgi_factory->MakeWindowAssociation(cg::utils::window::get_hwnd(), DXGI_MWA_NO_ALT_ENTER);
	temp_swap_chain.As(&swap_chain);
	frame_index = swap_chain->GetCurrentBackBufferIndex();

}

void cg::renderer::dx12_renderer::create_render_target_views()
{
	rtv_heap.create_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, frame_number);
	for (UINT i = 0; i < frame_number; i++)
	{
		THROW_IF_FAILED(swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i])));
		device->CreateRenderTargetView(render_targets[i].Get(), nullptr, rtv_heap.get_cpu_descriptor_handle(i));
		std::wstring name(L"Render target ");
		name += std::to_wstring(i);
		render_targets[i]->SetName(name.c_str());
	}
}

void cg::renderer::dx12_renderer::create_depth_buffer()
{
	CD3DX12_RESOURCE_DESC depth_buffer_desc(
		D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		0,
		settings->width,
		settings->height,
		1,
		1,
		DXGI_FORMAT_D32_FLOAT,
		1,
		0,
		D3D12_TEXTURE_LAYOUT_UNKNOWN,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);

	D3D12_CLEAR_VALUE clear_value{};
	clear_value.Format = DXGI_FORMAT_D32_FLOAT;
	clear_value.DepthStencil.Depth = 1;
	clear_value.DepthStencil.Stencil = 0;

	auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	THROW_IF_FAILED(device->CreateCommittedResource(
		&hp, 
		D3D12_HEAP_FLAG_NONE, 
		&depth_buffer_desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, 
		&clear_value, 
		IID_PPV_ARGS(&depth_buffer)));
	
	depth_buffer->SetName(L"Depth buffer");
	THROW_IF_FAILED(device->CreateCommittedResource(
		&hp, 
		D3D12_HEAP_FLAG_NONE, 
		&depth_buffer_desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clear_value,
		IID_PPV_ARGS(&shadow_map)));
	shadow_map->SetName(L"Shadow map");
	dsv_heap.create_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2);
	device->CreateDepthStencilView(depth_buffer.Get(), nullptr, dsv_heap.get_cpu_descriptor_handle(0));
	device->CreateDepthStencilView(shadow_map.Get(), nullptr, dsv_heap.get_cpu_descriptor_handle(1));
	

	dsv_heap.create_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	device->CreateDepthStencilView(
		depth_buffer.Get(),
		nullptr,
		dsv_heap.get_cpu_descriptor_handle()
	);
}

void cg::renderer::dx12_renderer::create_command_allocators()
{
	for (auto& command_allocator: command_allocators)
	{
		THROW_IF_FAILED(device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&command_allocator)
		));
	}
}

void cg::renderer::dx12_renderer::create_command_list()
{
	THROW_IF_FAILED(device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		command_allocators[0].Get(),
		pipeline_state.Get(),
		IID_PPV_ARGS(&command_list)
	));
}


void cg::renderer::dx12_renderer::load_pipeline()
{
	ComPtr<IDXGIFactory4> dxgi_factory = get_dxgi_factory();
	initialize_device(dxgi_factory);
	create_direct_command_queue();
	create_swap_chain(dxgi_factory);
	create_render_target_views();
	create_depth_buffer();
}

D3D12_STATIC_SAMPLER_DESC cg::renderer::dx12_renderer::get_sampler_descriptor()
{
	D3D12_STATIC_SAMPLER_DESC sampler_desc{};
	sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
	sampler_desc.MipLODBias = 0;
	sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler_desc.Filter = D3D12_FILTER_ANISOTROPIC;
	sampler_desc.MaxAnisotropy = 16;
	sampler_desc.ShaderRegister = 0;
	sampler_desc.RegisterSpace = 0;
	sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return sampler_desc;
}

void cg::renderer::dx12_renderer::create_root_signature(const D3D12_STATIC_SAMPLER_DESC* sampler_descriptors, UINT num_sampler_descriptors)
{
	CD3DX12_ROOT_PARAMETER1 root_parameters[3];
	CD3DX12_DESCRIPTOR_RANGE1 ranges[3];

	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	root_parameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

	ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
	root_parameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_FEATURE_DATA_ROOT_SIGNATURE data{};
	data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &data, sizeof(data))))
	{
		data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
	desc.Init_1_1(_countof(root_parameters), root_parameters, num_sampler_descriptors, sampler_descriptors,flags);

	ComPtr<ID3DBlob> signature, error;

	HRESULT res = D3DX12SerializeVersionedRootSignature(
		&desc, data.HighestVersion, &signature, &error
	);

	if (FAILED(res))
	{
		OutputDebugStringA((char*)error->GetBufferPointer());
		THROW_IF_FAILED(res);
	}

	THROW_IF_FAILED(device->CreateRootSignature(
		0, 
		signature->GetBufferPointer(), 
		signature->GetBufferSize(), 
		IID_PPV_ARGS(&root_signature))
	);
}

std::filesystem::path cg::renderer::dx12_renderer::get_shader_path()
{
	return std::filesystem::path(settings->shader_path);
}

ComPtr<ID3DBlob> cg::renderer::dx12_renderer::compile_shader(const std::string& entrypoint, const std::string& target)
{
	ComPtr<ID3DBlob> shader, error;
	UINT compile_flags = 0;
	#ifdef _DEBUG
	compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	#endif

	HRESULT res = D3DCompileFromFile(
		get_shader_path().wstring().c_str(),
		nullptr,
		nullptr,
		entrypoint.c_str(),
		target.c_str(),
		compile_flags,
		0,
		&shader,
		&error
	);

	if (FAILED(res))
	{
		OutputDebugStringA((char*)error->GetBufferPointer());
		MessageBoxA(NULL, (char*)error->GetBufferPointer(), "Runtime failure", MB_OK);
		THROW_IF_FAILED(res);
	}

	return shader;
}

void cg::renderer::dx12_renderer::create_pso()
{
	ComPtr<ID3DBlob> vertex_shader = compile_shader("VSMain", "vs_5_0");
	ComPtr<ID3DBlob> pixel_shader = compile_shader("PSMain", "ps_5_0");
	ComPtr<ID3DBlob> pixel_shader_texture = compile_shader("PSMain_texture", "ps_5_0");
	ComPtr<ID3DBlob> vertex_shader_shadow_map = compile_shader("VSShadowMap", "vs_5_0");

	D3D12_INPUT_ELEMENT_DESC input_descs[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.InputLayout = {input_descs, _countof(input_descs)};
	desc.pRootSignature = root_signature.Get();
	desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader.Get());
	desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader.Get());
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.FrontCounterClockwise = TRUE;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.SampleMask = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;	
	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	desc.SampleDesc.Count = 1;
	
	THROW_IF_FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)));
	desc.PS = CD3DX12_SHADER_BYTECODE(pixel_shader_texture.Get());
	THROW_IF_FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state_texture)));

	desc.VS = CD3DX12_SHADER_BYTECODE(vertex_shader_shadow_map.Get());
	desc.PS = CD3DX12_SHADER_BYTECODE(0, 0);
	desc.NumRenderTargets = 0;
	desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	THROW_IF_FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state_shadow)));
}

void cg::renderer::dx12_renderer::create_resource_on_upload_heap(ComPtr<ID3D12Resource>& resource, UINT size, const std::wstring& name)
{
	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	THROW_IF_FAILED(device->CreateCommittedResource(
		&hp,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource)
	));
	if (!name.empty())
	{
		resource->SetName(name.c_str());
	}
}

void cg::renderer::dx12_renderer::create_resource_on_default_heap(ComPtr<ID3D12Resource>& resource, UINT size, const std::wstring& name, D3D12_RESOURCE_DESC* resource_descriptor)
{
	CD3DX12_RESOURCE_DESC default_descriptor;
	
	if (resource_descriptor == nullptr)
	{
		default_descriptor = CD3DX12_RESOURCE_DESC::Buffer(size);
		resource_descriptor = &default_descriptor;
	}

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	THROW_IF_FAILED(device->CreateCommittedResource(
		&hp,
		D3D12_HEAP_FLAG_NONE,
		resource_descriptor,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&resource)
	));
	if (!name.empty())
	{
		resource->SetName(name.c_str());
	}
}

void cg::renderer::dx12_renderer::copy_data(const void* buffer_data, UINT buffer_size, ComPtr<ID3D12Resource>& destination_resource)
{
	UINT8* buffer_data_begin;
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(
		destination_resource->Map(
			0, &read_range, reinterpret_cast<void**>(&buffer_data_begin)
		)
	);
	memcpy(buffer_data_begin, buffer_data, buffer_size);
	destination_resource->Unmap(0, 0);
}

void cg::renderer::dx12_renderer::copy_data(const void* buffer_data, const UINT buffer_size, ComPtr<ID3D12Resource>& destination_resource, ComPtr<ID3D12Resource>& intermediate_resource, D3D12_RESOURCE_STATES state_after, int row_pitch, int slice_pitch)
{
	D3D12_SUBRESOURCE_DATA data{};
	data.pData = buffer_data;
	data.RowPitch = row_pitch != 0 ? row_pitch : buffer_size;
	data.SlicePitch = slice_pitch != 0 ? slice_pitch : buffer_size;

	UpdateSubresources(command_list.Get(), destination_resource.Get(), intermediate_resource.Get(), 0, 0, 1, &data);
	auto bar = CD3DX12_RESOURCE_BARRIER::Transition(
		destination_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, state_after);
	command_list->ResourceBarrier(1, &bar);
}

D3D12_VERTEX_BUFFER_VIEW cg::renderer::dx12_renderer::create_vertex_buffer_view(const ComPtr<ID3D12Resource>& vertex_buffer, const UINT vertex_buffer_size)
{
	D3D12_VERTEX_BUFFER_VIEW view{};
	view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
	view.StrideInBytes = sizeof(vertex);
	view.SizeInBytes = vertex_buffer_size;
	return view;
}

D3D12_INDEX_BUFFER_VIEW cg::renderer::dx12_renderer::create_index_buffer_view(const ComPtr<ID3D12Resource>& index_buffer, const UINT index_buffer_size)
{
	D3D12_INDEX_BUFFER_VIEW view{};
	view.BufferLocation = index_buffer->GetGPUVirtualAddress();
	view.SizeInBytes = index_buffer_size;
	view.Format = DXGI_FORMAT_R32_UINT;
	return view;
}

void cg::renderer::dx12_renderer::create_shader_resource_view(const ComPtr<ID3D12Resource>& texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handler)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv_desc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(texture.Get(), &srv_desc, cpu_handler);
}

void cg::renderer::dx12_renderer::create_constant_buffer_view(const ComPtr<ID3D12Resource>& buffer, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handler)
{
	D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
	desc.BufferLocation = buffer->GetGPUVirtualAddress();
	desc.SizeInBytes = (sizeof(cb) + 255) & ~255;
	device->CreateConstantBufferView(&desc, cpu_handler);
}

void cg::renderer::dx12_renderer::load_assets()
{
	auto sampler_desc = get_sampler_descriptor();
	create_root_signature(&sampler_desc, 1);
	create_pso();
	create_command_allocators();
	create_command_list();
	const size_t shape_num = model->get_index_buffers().size();

	cbv_srv_heap.create_heap(
		device, 
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
		3 + shape_num, 
		D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	
	upload_vertex_buffers.resize(shape_num);
	upload_index_buffers.resize(shape_num);

	vertex_buffers.resize(shape_num);
	vertex_buffer_views.resize(shape_num);
	index_buffers.resize(shape_num);
	index_buffer_views.resize(shape_num);
	upload_textures.resize(shape_num);
	textures.resize(shape_num);

	for (size_t i = 0; i < shape_num ; i++)
	{
		auto vb_data = model->get_vertex_buffers()[i];
		const UINT vb_size = static_cast<UINT>(vb_data->size_bytes());
		std::wstring vb_name (L"Vertex buffer ");
		vb_name += std::to_wstring(i);
		create_resource_on_default_heap(vertex_buffers[i], vb_size, vb_name);
		create_resource_on_upload_heap(upload_vertex_buffers[i], vb_size, vb_name);
		copy_data(vb_data -> get_data(), vb_size, vertex_buffers[i], upload_vertex_buffers[i], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		vertex_buffer_views[i] = create_vertex_buffer_view(vertex_buffers[i], vb_size);

		auto ib_data = model->get_index_buffers()[i];
		const UINT ib_size = static_cast<UINT>(ib_data->size_bytes());
		std::wstring ib_name (L"Index buffer ");
		ib_name += std::to_wstring(i);
		create_resource_on_default_heap(index_buffers[i], ib_size, ib_name);
		create_resource_on_upload_heap(upload_index_buffers[i], ib_size, ib_name);
		copy_data(ib_data -> get_data(), ib_size, index_buffers[i], upload_index_buffers[i], D3D12_RESOURCE_STATE_INDEX_BUFFER);
		index_buffer_views[i] = create_index_buffer_view(index_buffers[i], ib_size);

		if (model->get_per_shape_texture_files()[i].empty())
		{
			continue;
		}

		auto full_name = std::filesystem::absolute(model->get_per_shape_texture_files()[i]).string();
		int tex_width, tex_height, tex_channels;
		unsigned char * image = stbi_load(
			full_name.c_str(), &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha
		);

		if (image == nullptr)
		{
			throw std::runtime_error("can't load texture");
		}

		D3D12_RESOURCE_DESC texture_desc{};
		texture_desc.MipLevels = 1;
		texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texture_desc.Width = tex_width;
		texture_desc.Height = tex_height;
		texture_desc.DepthOrArraySize = 1;
		texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		texture_desc.SampleDesc.Count = 1;
		texture_desc.SampleDesc.Quality = 0;

		create_resource_on_default_heap(textures[i], 0, model->get_per_shape_texture_files()[i].wstring(), &texture_desc);
		const UINT upload_buffer_size = GetRequiredIntermediateSize(textures[i].Get(), 0, 1);
		create_resource_on_upload_heap(upload_textures[i], upload_buffer_size);
		copy_data(image, upload_buffer_size, textures[i], upload_textures[i], 
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		tex_width * STBI_rgb_alpha, tex_width * STBI_rgb_alpha * tex_height);
		create_shader_resource_view(textures[i], cbv_srv_heap.get_cpu_descriptor_handle(i + 3));
	}

	std::wstring cd_name(L"Constant buffer ");
	create_resource_on_upload_heap(constant_buffer, 64*1024, cd_name);
	copy_data(&cb, sizeof(cb), constant_buffer);
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&constant_buffer_data_begin)));
	create_constant_buffer_view(constant_buffer, cbv_srv_heap.get_cpu_descriptor_handle());
	D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc{};
	null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	null_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	null_srv_desc.Texture2D.MipLevels = 1;
	null_srv_desc.Texture2D.MostDetailedMip = 0;
	null_srv_desc.Texture2D.ResourceMinLODClamp = 0.f;
	device->CreateShaderResourceView(nullptr, &null_srv_desc, cbv_srv_heap.get_cpu_descriptor_handle(1));

	D3D12_SHADER_RESOURCE_VIEW_DESC shadow_srv_desc{};
	shadow_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shadow_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shadow_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
	shadow_srv_desc.Texture2D.MipLevels = 1;
	shadow_srv_desc.Texture2D.MostDetailedMip = 0;
	shadow_srv_desc.Texture2D.ResourceMinLODClamp = 0.f;
	device->CreateShaderResourceView(shadow_map.Get(), &shadow_srv_desc, cbv_srv_heap.get_cpu_descriptor_handle(2));

	THROW_IF_FAILED(command_list->Close());

	ID3D12CommandList* command_lists [] = {command_list.Get()};
	command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

	THROW_IF_FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fence_event == nullptr){
		THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
	}

	wait_for_gpu();
}


void cg::renderer::dx12_renderer::populate_command_list()
{
	THROW_IF_FAILED(command_allocators[frame_index]->Reset());
	THROW_IF_FAILED(command_list->Reset(command_allocators[frame_index].Get(), pipeline_state_shadow.Get()));

	command_list->SetGraphicsRootSignature(root_signature.Get());
	ID3D12DescriptorHeap* heaps[] = {cbv_srv_heap.get()};
	command_list->SetDescriptorHeaps(_countof(heaps), heaps);
	command_list->SetGraphicsRootDescriptorTable(0, cbv_srv_heap.get_gpu_descriptor_handle(0));
	command_list->SetGraphicsRootDescriptorTable(2, cbv_srv_heap.get_gpu_descriptor_handle(1));
	command_list->RSSetScissorRects(1, &scissor_rect);
	command_list->RSSetViewports(1, &view_port);

	auto shadow_map_desc = dsv_heap.get_cpu_descriptor_handle(1);
	command_list->OMSetRenderTargets(0, nullptr, FALSE, &shadow_map_desc);
	command_list->ClearDepthStencilView(shadow_map_desc,
	D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (size_t s = 0; s < model->get_index_buffers().size(); s++)
	{
		command_list->IASetVertexBuffers(0, 1, &vertex_buffer_views[s]);
		command_list->IASetIndexBuffer(&index_buffer_views[s]);
		command_list->DrawIndexedInstanced(
			static_cast<UINT>(model->get_index_buffers()[s]->count()),
			1, 0, 0, 0
		);
	}

	D3D12_RESOURCE_BARRIER begin_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			render_targets[frame_index].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			shadow_map.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		)
	};
	command_list->ResourceBarrier(_countof(begin_barriers), begin_barriers);

	command_list->SetPipelineState(pipeline_state.Get());
	auto handle = rtv_heap.get_cpu_descriptor_handle(frame_index);
	auto dsv = dsv_heap.get_cpu_descriptor_handle();
	command_list->OMSetRenderTargets(1, &handle, FALSE, &dsv);
	const float clear_color[] = {0.f, 0.f, 0.f, 1.f};
	command_list->ClearRenderTargetView(handle, clear_color, 0, nullptr);
	command_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	command_list->SetGraphicsRootDescriptorTable(2, cbv_srv_heap.get_gpu_descriptor_handle(2));

	for (size_t s = 0; s < model->get_index_buffers().size(); s++)
	{
		if (!model->get_per_shape_texture_files()[s].empty())
		{
			continue;
		}
		command_list->IASetVertexBuffers(0, 1, &vertex_buffer_views[s]);
		command_list->IASetIndexBuffer(&index_buffer_views[s]);
		command_list->DrawIndexedInstanced(
			static_cast<UINT>(model->get_index_buffers()[s]->count()),
			1, 0, 0, 0
		);
	}

	command_list->SetPipelineState(pipeline_state_texture.Get());

	for (size_t s = 0; s < model->get_index_buffers().size(); s++)
	{
		if (model->get_per_shape_texture_files()[s].empty())
		{
			continue;
		}
		command_list->SetGraphicsRootDescriptorTable(1, cbv_srv_heap.get_gpu_descriptor_handle(3+s));
		command_list->IASetVertexBuffers(0, 1, &vertex_buffer_views[s]);
		command_list->IASetIndexBuffer(&index_buffer_views[s]);
		command_list->DrawIndexedInstanced(
			static_cast<UINT>(model->get_index_buffers()[s]->count()),
			1, 0, 0, 0
		);
	}

	D3D12_RESOURCE_BARRIER end_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			render_targets[frame_index].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			shadow_map.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		)
	};

	command_list->ResourceBarrier(_countof(end_barriers), end_barriers);
	THROW_IF_FAILED(command_list->Close());
	
}


void cg::renderer::dx12_renderer::move_to_next_frame()
{
	const UINT64 current_fence_value = fence_values[frame_index];
	THROW_IF_FAILED(command_queue->Signal(fence.Get(), current_fence_value));
	frame_index = swap_chain->GetCurrentBackBufferIndex();
	if (fence->GetCompletedValue() < fence_values[frame_index])
	{
		THROW_IF_FAILED(fence->SetEventOnCompletion(fence_values[frame_index], fence_event));
		WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
	}
	fence_values[frame_index] = current_fence_value + 1;
}

void cg::renderer::dx12_renderer::wait_for_gpu()
{
	THROW_IF_FAILED(command_queue->Signal(fence.Get(), fence_values[frame_index]));
	THROW_IF_FAILED(fence->SetEventOnCompletion(fence_values[frame_index], fence_event));
	WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
	fence_values[frame_index]++;
}


void cg::renderer::descriptor_heap::create_heap(ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT number, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.NumDescriptors = number;
	desc.Type = type;
	desc.Flags = flags;
	
	THROW_IF_FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));
	descriptor_size = device->GetDescriptorHandleIncrementSize(type);
}

D3D12_CPU_DESCRIPTOR_HANDLE cg::renderer::descriptor_heap::get_cpu_descriptor_handle(UINT index) const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(index), descriptor_size);
}

D3D12_GPU_DESCRIPTOR_HANDLE cg::renderer::descriptor_heap::get_gpu_descriptor_handle(UINT index) const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), static_cast<INT>(index), descriptor_size);
}
ID3D12DescriptorHeap* cg::renderer::descriptor_heap::get() const
{
	return heap.Get();
}
