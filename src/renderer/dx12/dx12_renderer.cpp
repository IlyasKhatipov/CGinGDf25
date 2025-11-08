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
}

D3D12_STATIC_SAMPLER_DESC cg::renderer::dx12_renderer::get_sampler_descriptor()
{
	D3D12_STATIC_SAMPLER_DESC sampler_desc{};
	return sampler_desc;
}

void cg::renderer::dx12_renderer::create_root_signature(const D3D12_STATIC_SAMPLER_DESC* sampler_descriptors, UINT num_sampler_descriptors)
{
	CD3DX12_ROOT_PARAMETER1 root_parameters[1];
	CD3DX12_DESCRIPTOR_RANGE1 ranges[1];

	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
	root_parameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

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
		THROW_IF_FAILED(res);
	}

	return shader;
}

void cg::renderer::dx12_renderer::create_pso()
{
	ComPtr<ID3DBlob> vertex_shader = compile_shader("VSMain", "vs_5_0");
	ComPtr<ID3DBlob> pixel_shader = compile_shader("PSMain", "ps_5_0");
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
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.SampleMask = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;

	THROW_IF_FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_state)));
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
	create_root_signature(nullptr, 0);
	create_pso();
	create_command_allocators();
	create_command_list();

	cbv_srv_heap.create_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	const size_t shape_num = model->get_index_buffers().size();

	vertex_buffers.resize(shape_num);
	vertex_buffer_views.resize(shape_num);
	index_buffers.resize(shape_num);
	index_buffer_views.resize(shape_num);

	for (size_t i = 0; i < shape_num ; i++)
	{
		auto vb_data = model->get_vertex_buffers()[i];
		const UINT vb_size = static_cast<UINT>(vb_data->size_bytes());
		std::wstring vb_name (L"Vertex buffer ");
		vb_name += std::to_wstring(i);
		create_resource_on_upload_heap(vertex_buffers[i], vb_size, vb_name);
		copy_data(vb_data -> get_data(), vb_size, vertex_buffers[i]);
		vertex_buffer_views[i] = create_vertex_buffer_view(vertex_buffers[i], vb_size);

		auto ib_data = model->get_index_buffers()[i];
		const UINT ib_size = static_cast<UINT>(ib_data->size_bytes());
		std::wstring ib_name (L"Index buffer ");
		ib_name += std::to_wstring(i);
		create_resource_on_upload_heap(index_buffers[i], ib_size, ib_name);
		copy_data(ib_data -> get_data(), ib_size, index_buffers[i]);
		index_buffer_views[i] = create_index_buffer_view(index_buffers[i], ib_size);
	}

	std::wstring cd_name(L"Constant buffer ");
	create_resource_on_upload_heap(constant_buffer, 64*1024, cd_name);
	copy_data(&cb, sizeof(cb), constant_buffer);
	CD3DX12_RANGE read_range(0, 0);
	THROW_IF_FAILED(constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&constant_buffer_data_begin)));
	create_constant_buffer_view(constant_buffer, cbv_srv_heap.get_cpu_descriptor_handle());

	THROW_IF_FAILED(command_list->Close());

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
	THROW_IF_FAILED(command_list->Reset(command_allocators[frame_index].Get(), pipeline_state.Get()));

	command_list->SetGraphicsRootSignature(root_signature.Get());
	ID3D12DescriptorHeap* heaps[] = {cbv_srv_heap.get()};
	command_list->SetDescriptorHeaps(_countof(heaps), heaps);
	command_list->SetGraphicsRootDescriptorTable(0, cbv_srv_heap.get_gpu_descriptor_handle(0));
	command_list->RSSetScissorRects(1, &scissor_rect);
	command_list->RSSetViewports(1, &view_port);

	D3D12_RESOURCE_BARRIER begin_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			render_targets[frame_index].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	};
	command_list->ResourceBarrier(_countof(begin_barriers), begin_barriers);

	auto handle = rtv_heap.get_cpu_descriptor_handle(frame_index);
	command_list->OMSetRenderTargets(1, &handle, FALSE, nullptr);
	const float clear_color[] = {0.f, 0.f, 0.f, 1.f};
	command_list->ClearRenderTargetView(handle, clear_color, 0, nullptr);
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

	D3D12_RESOURCE_BARRIER end_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			render_targets[frame_index].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
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
