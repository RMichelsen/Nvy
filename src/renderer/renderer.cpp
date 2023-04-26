#include "renderer.h"
#include "renderer/glyph_renderer.h"

void InitializeD2D(Renderer *renderer) {
	D2D1_FACTORY_OPTIONS options {};
#ifndef NDEBUG
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

	WIN_CHECK(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, &renderer->d2d_factory));
}

void InitializeD3D(Renderer *renderer) {
	uint32_t flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	// Force DirectX 11.1
	ID3D11Device *temp_device;
	ID3D11DeviceContext *temp_context;
	D3D_FEATURE_LEVEL feature_levels[] = { 
		D3D_FEATURE_LEVEL_11_1,         
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1 
	};
	WIN_CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, feature_levels,
		ARRAYSIZE(feature_levels), D3D11_SDK_VERSION, &temp_device, &renderer->d3d_feature_level, &temp_context));
	WIN_CHECK(temp_device->QueryInterface(__uuidof(ID3D11Device2), reinterpret_cast<void **>(&renderer->d3d_device)));
	WIN_CHECK(temp_context->QueryInterface(__uuidof(ID3D11DeviceContext2), reinterpret_cast<void **>(&renderer->d3d_context)));

	IDXGIDevice3 *dxgi_device;
	WIN_CHECK(renderer->d3d_device->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void **>(&dxgi_device)));
	WIN_CHECK(renderer->d2d_factory->CreateDevice(dxgi_device, &renderer->d2d_device));
	WIN_CHECK(renderer->d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, &renderer->d2d_context));
	WIN_CHECK(renderer->d2d_context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &renderer->d2d_background_rect_brush));

	SafeRelease(&dxgi_device);
}

void InitializeDWrite(Renderer *renderer) {
	WIN_CHECK(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory4), reinterpret_cast<IUnknown **>(&renderer->dwrite_factory)));
	if(renderer->disable_ligatures) {
		WIN_CHECK(renderer->dwrite_factory->CreateTypography(&renderer->dwrite_typography));
		WIN_CHECK(renderer->dwrite_typography->AddFontFeature(DWRITE_FONT_FEATURE {
			.nameTag = DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES,
			.parameter = 0		
		}));
	}
}

void HandleDeviceLost(Renderer *renderer);
void InitializeWindowDependentResources(Renderer *renderer, uint32_t width, uint32_t height) {
	// Initializing window resources invalidates previous draws to the window,
	// so all lines need to be redrawn to make sure they are preserved.
	// Otherwise lines will appear entirely black until updated.
	renderer->draws_invalidated = true;

	renderer->pixel_size.width = width;
	renderer->pixel_size.height = height;

	ID3D11RenderTargetView *null_views[] = { nullptr };
	renderer->d3d_context->OMSetRenderTargets(ARRAYSIZE(null_views), null_views, nullptr);
	renderer->d2d_context->SetTarget(nullptr);
	renderer->d3d_context->Flush();

	if (renderer->dxgi_swapchain) {
		renderer->d2d_target_bitmap->Release();

		HRESULT hr = renderer->dxgi_swapchain->ResizeBuffers(
			2,
			width,
			height,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		);

		if (hr == DXGI_ERROR_DEVICE_REMOVED) {
			HandleDeviceLost(renderer);
		}
	}
	else {
		DXGI_SWAP_CHAIN_DESC1 swapchain_desc {
			.Width = width,
			.Height = height,
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0
			},
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_NONE,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		};

		IDXGIDevice3 *dxgi_device;
		WIN_CHECK(renderer->d3d_device->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void **>(&dxgi_device)));
		IDXGIAdapter *dxgi_adapter;
		WIN_CHECK(dxgi_device->GetAdapter(&dxgi_adapter));
		IDXGIFactory2 *dxgi_factory;
		WIN_CHECK(dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory)));

		IDXGISwapChain1 *dxgi_swapchain_temp;
		WIN_CHECK(dxgi_factory->CreateSwapChainForHwnd(renderer->d3d_device,
			renderer->hwnd, &swapchain_desc, nullptr, nullptr, &dxgi_swapchain_temp));
		WIN_CHECK(dxgi_factory->MakeWindowAssociation(renderer->hwnd, DXGI_MWA_NO_ALT_ENTER));
		WIN_CHECK(dxgi_swapchain_temp->QueryInterface(__uuidof(IDXGISwapChain2), 
					reinterpret_cast<void **>(&renderer->dxgi_swapchain)));

		WIN_CHECK(renderer->dxgi_swapchain->SetMaximumFrameLatency(1));
		renderer->swapchain_wait_handle = renderer->dxgi_swapchain->GetFrameLatencyWaitableObject();

		SafeRelease(&dxgi_swapchain_temp);
		SafeRelease(&dxgi_device);
		SafeRelease(&dxgi_adapter);
		SafeRelease(&dxgi_factory);
	}

	constexpr D2D1_BITMAP_PROPERTIES1 target_bitmap_properties {
		.pixelFormat = D2D1_PIXEL_FORMAT {
			.format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.alphaMode = D2D1_ALPHA_MODE_IGNORE
		},
		.dpiX = DEFAULT_DPI,
		.dpiY = DEFAULT_DPI,
		.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW
	};
	IDXGISurface2 *dxgi_backbuffer;
	WIN_CHECK(renderer->dxgi_swapchain->GetBuffer(0, IID_PPV_ARGS(&dxgi_backbuffer)));
	WIN_CHECK(renderer->d2d_context->CreateBitmapFromDxgiSurface(
		dxgi_backbuffer,
		&target_bitmap_properties,
		&renderer->d2d_target_bitmap
	));
	renderer->d2d_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

	SafeRelease(&dxgi_backbuffer);
}

void HandleDeviceLost(Renderer *renderer) {
	SafeRelease(&renderer->d3d_device);
	SafeRelease(&renderer->d3d_context);
	SafeRelease(&renderer->dxgi_swapchain);
	SafeRelease(&renderer->d2d_factory);
	SafeRelease(&renderer->d2d_device);
	SafeRelease(&renderer->d2d_context);
	SafeRelease(&renderer->d2d_target_bitmap);
	SafeRelease(&renderer->d2d_background_rect_brush);
	SafeRelease(&renderer->dwrite_factory);
	SafeRelease(&renderer->dwrite_text_format);
	delete renderer->glyph_renderer;

	InitializeD2D(renderer);
	InitializeD3D(renderer);
	InitializeDWrite(renderer);
	RECT client_rect;
	GetClientRect(renderer->hwnd, &client_rect);
	InitializeWindowDependentResources(
		renderer,
		static_cast<uint32_t>(client_rect.right - client_rect.left),
		static_cast<uint32_t>(client_rect.bottom - client_rect.top)
	);
}

void RendererInitialize(Renderer *renderer, HWND hwnd, bool disable_ligatures, float linespace_factor, float monitor_dpi) {
	renderer->hwnd = hwnd;
	renderer->disable_ligatures = disable_ligatures;
	renderer->linespace_factor = linespace_factor;

	renderer->dpi_scale = monitor_dpi / 96.0f;
	renderer->hl_attribs.resize(MAX_HIGHLIGHT_ATTRIBS);

	wcscpy_s(renderer->fallback_font, MAX_FONT_LENGTH, L"Consolas");

	InitializeD2D(renderer);
	InitializeD3D(renderer);
	InitializeDWrite(renderer);
	renderer->glyph_renderer = new GlyphRenderer(renderer);
	RendererUpdateFont(renderer, DEFAULT_FONT_SIZE, DEFAULT_FONT, static_cast<int>(strlen(DEFAULT_FONT)));
}

void RendererAttach(Renderer *renderer) {
	RECT client_rect;
	GetClientRect(renderer->hwnd, &client_rect);
	InitializeWindowDependentResources(
		renderer,
		static_cast<uint32_t>(client_rect.right - client_rect.left),
		static_cast<uint32_t>(client_rect.bottom - client_rect.top)
	);
}

void RendererShutdown(Renderer *renderer) {
	SafeRelease(&renderer->d3d_device);
	SafeRelease(&renderer->d3d_context);
	SafeRelease(&renderer->dxgi_swapchain);
	SafeRelease(&renderer->d2d_factory);
	SafeRelease(&renderer->d2d_device);
	SafeRelease(&renderer->d2d_context);
	SafeRelease(&renderer->d2d_target_bitmap);
	SafeRelease(&renderer->d2d_background_rect_brush);
	SafeRelease(&renderer->dwrite_factory);
	SafeRelease(&renderer->dwrite_text_format);
	delete renderer->glyph_renderer;

	free(renderer->grid_chars);
	free(renderer->grid_cell_properties);
}

void RendererResize(Renderer *renderer, uint32_t width, uint32_t height) {
	InitializeWindowDependentResources(renderer, width, height);
}

float GetTextWidth(Renderer *renderer, wchar_t *text, uint32_t length) {
	// Create dummy text format to hit test the width of the font
	IDWriteTextLayout *test_text_layout = nullptr;
	WIN_CHECK(renderer->dwrite_factory->CreateTextLayout(
		text,
		length,
		renderer->dwrite_text_format,
		0.0f,
		0.0f,
		&test_text_layout
	));

	DWRITE_HIT_TEST_METRICS metrics;
	float _;
	WIN_CHECK(test_text_layout->HitTestTextPosition(0, 0, &_, &_, &metrics));
	test_text_layout->Release();

	return metrics.width;
}

bool UpdateFontMetrics(Renderer *renderer, float font_size, const char* font_string, int strlen) {
	font_size = max(5.0f, min(font_size, 150.0f));
	renderer->last_requested_font_size = font_size;

	IDWriteFontCollection *font_collection;
	WIN_CHECK(renderer->dwrite_factory->GetSystemFontCollection(&font_collection));

	int wstrlen = MultiByteToWideChar(CP_UTF8, 0, font_string, strlen, 0, 0);
	if (wstrlen != 0 && wstrlen < MAX_FONT_LENGTH) {
		MultiByteToWideChar(CP_UTF8, 0, font_string, strlen, renderer->font, MAX_FONT_LENGTH - 1);
		renderer->font[wstrlen] = L'\0';
	}

	uint32_t index;
	BOOL exists;
	font_collection->FindFamilyName(renderer->font, &index, &exists);

	bool guifont_exists = true;
	if (!exists) {
		guifont_exists = false;
		font_collection->FindFamilyName(renderer->fallback_font, &index, &exists);
		// Reset fallback font if it doesn't exist
		if (!exists) {
			wcscpy_s(renderer->fallback_font, MAX_FONT_LENGTH, L"Consolas");
			font_collection->FindFamilyName(renderer->fallback_font, &index, &exists);
		}
		memcpy(renderer->font, renderer->fallback_font, (wcslen(renderer->fallback_font) + 1) * sizeof(wchar_t));
	}

	IDWriteFontFamily *font_family;
	WIN_CHECK(font_collection->GetFontFamily(index, &font_family));

	IDWriteFont *write_font;
	WIN_CHECK(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &write_font));

	IDWriteFontFace *font_face;
	WIN_CHECK(write_font->CreateFontFace(&font_face));
	WIN_CHECK(font_face->QueryInterface<IDWriteFontFace1>(&renderer->font_face));

	renderer->font_face->GetMetrics(&renderer->font_metrics);

	uint16_t glyph_index;
	constexpr uint32_t codepoint = L'A';
	WIN_CHECK(renderer->font_face->GetGlyphIndicesW(&codepoint, 1, &glyph_index));

	int32_t glyph_advance_in_em;
	WIN_CHECK(renderer->font_face->GetDesignGlyphAdvances(1, &glyph_index, &glyph_advance_in_em));

	IDWriteFont* write_font_bold;
	WIN_CHECK(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &write_font_bold));

	IDWriteFontFace* font_face_bold;
	WIN_CHECK(write_font_bold->CreateFontFace(&font_face_bold));
	IDWriteFontFace1* font_size_scale_bold1;
	WIN_CHECK(font_face_bold->QueryInterface<IDWriteFontFace1>(&font_size_scale_bold1));
	DWRITE_FONT_METRICS1 font_metrics_bold;
	font_size_scale_bold1->GetMetrics(&font_metrics_bold);

	int32_t glyph_advance_in_em_bold;
	WIN_CHECK(font_size_scale_bold1->GetDesignGlyphAdvances(1, &glyph_index, &glyph_advance_in_em_bold));

	float desired_height = font_size * renderer->dpi_scale * (DEFAULT_DPI / POINTS_PER_INCH);
	float width_advance = static_cast<float>(glyph_advance_in_em) / renderer->font_metrics.designUnitsPerEm;
	float desired_width = desired_height * width_advance;

	float width_advance_bold = static_cast<float>(glyph_advance_in_em_bold) / font_metrics_bold.designUnitsPerEm;
	float desired_width_bold = desired_height * width_advance_bold;

	float bold_scale = desired_width / desired_width_bold;
	// We need the width to be aligned on a per-pixel boundary, thus we will
	// roundf the desired_width and calculate the font size given the new exact width
	renderer->font_width = roundf(desired_width);
	renderer->font_size = renderer->font_width / width_advance;

	renderer->font_size_scale_bold = renderer->font_size * bold_scale;

	float frac_font_ascent = (renderer->font_size * renderer->font_metrics.ascent) / renderer->font_metrics.designUnitsPerEm;
	float frac_font_descent = (renderer->font_size * renderer->font_metrics.descent) / renderer->font_metrics.designUnitsPerEm;
	float linegap = (renderer->font_size * renderer->font_metrics.lineGap) / renderer->font_metrics.designUnitsPerEm;
	float half_linegap = linegap / 2.0f;
	renderer->font_ascent = ceilf(frac_font_ascent + half_linegap);
	renderer->font_descent = ceilf(frac_font_descent + half_linegap);
	renderer->font_height = renderer->font_ascent + renderer->font_descent;
	renderer->font_height *= renderer->linespace_factor;

	WIN_CHECK(renderer->dwrite_factory->CreateTextFormat(
		renderer->font,
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		renderer->font_size,
		L"en-us",
		&renderer->dwrite_text_format
	));

	WIN_CHECK(renderer->dwrite_text_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, renderer->font_height, renderer->font_ascent * renderer->linespace_factor));
	WIN_CHECK(renderer->dwrite_text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
	WIN_CHECK(renderer->dwrite_text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

	SafeRelease(&font_face);
	SafeRelease(&font_face_bold);
	SafeRelease(&write_font);
	SafeRelease(&write_font_bold);

	SafeRelease(&font_family);
	SafeRelease(&font_collection);

	return guifont_exists;
}

bool RendererUpdateFont(Renderer *renderer, float font_size, const char *font_string, int strlen) {
	if (renderer->dwrite_text_format) {
		renderer->dwrite_text_format->Release();
	}

	renderer->draws_invalidated = true;
	return UpdateFontMetrics(renderer, font_size, font_string, strlen);
}

void UpdateDefaultColors(Renderer *renderer, mpack_node_t default_colors) {
	size_t default_colors_arr_length = mpack_node_array_length(default_colors);

	for (size_t i = 1; i < default_colors_arr_length; ++i) {
		mpack_node_t color_arr = mpack_node_array_at(default_colors, i);

		// Default colors occupy the first index of the highlight attribs array
		renderer->hl_attribs[0].foreground = static_cast<uint32_t>(mpack_node_array_at(color_arr, 0).data->value.u);
		renderer->hl_attribs[0].background = static_cast<uint32_t>(mpack_node_array_at(color_arr, 1).data->value.u);
		renderer->hl_attribs[0].special = static_cast<uint32_t>(mpack_node_array_at(color_arr, 2).data->value.u);
		renderer->hl_attribs[0].flags = 0;
	}
}

void UpdateHighlightAttributes(Renderer *renderer, mpack_node_t highlight_attribs) {
	uint64_t attrib_count = mpack_node_array_length(highlight_attribs);
	for (uint64_t i = 1; i < attrib_count; ++i) {
		int64_t attrib_index = mpack_node_array_at(mpack_node_array_at(highlight_attribs, i), 0).data->value.i;
		assert(attrib_index <= MAX_HIGHLIGHT_ATTRIBS);

		mpack_node_t attrib_map = mpack_node_array_at(mpack_node_array_at(highlight_attribs, i), 1);

		const auto SetColor = [&](const char *name, uint32_t *color) {
			mpack_node_t color_node = mpack_node_map_cstr_optional(attrib_map, name);
			if (!mpack_node_is_missing(color_node)) {
				*color = static_cast<uint32_t>(color_node.data->value.u);
			}
			else {
				*color = DEFAULT_COLOR;
			}
		};
		SetColor("foreground", &renderer->hl_attribs[attrib_index].foreground);
		SetColor("background", &renderer->hl_attribs[attrib_index].background);
		SetColor("special", &renderer->hl_attribs[attrib_index].special);

		const auto SetFlag = [&](const char *flag_name, HighlightAttributeFlags flag) {
			mpack_node_t flag_node = mpack_node_map_cstr_optional(attrib_map, flag_name);
			if (!mpack_node_is_missing(flag_node)) {
				if (flag_node.data->value.b) {
					renderer->hl_attribs[attrib_index].flags |= flag;
				}
				else {
					renderer->hl_attribs[attrib_index].flags &= ~flag;
				}
			}
		};
		SetFlag("reverse", HL_ATTRIB_REVERSE);
		SetFlag("italic", HL_ATTRIB_ITALIC);
		SetFlag("bold", HL_ATTRIB_BOLD);
		SetFlag("strikethrough", HL_ATTRIB_STRIKETHROUGH);
		SetFlag("underline", HL_ATTRIB_UNDERLINE);
		SetFlag("undercurl", HL_ATTRIB_UNDERCURL);
	}
}

uint32_t CreateForegroundColor(Renderer *renderer, HighlightAttributes *hl_attribs) {
	if (hl_attribs->flags & HL_ATTRIB_REVERSE) {
		return hl_attribs->background == DEFAULT_COLOR ? renderer->hl_attribs[0].background : hl_attribs->background;
	}
	else {
		return hl_attribs->foreground == DEFAULT_COLOR ? renderer->hl_attribs[0].foreground : hl_attribs->foreground;
	}
}

uint32_t CreateBackgroundColor(Renderer *renderer, HighlightAttributes *hl_attribs) {
	if (hl_attribs->flags & HL_ATTRIB_REVERSE) {
		return hl_attribs->foreground == DEFAULT_COLOR ? renderer->hl_attribs[0].foreground : hl_attribs->foreground;
	}
	else {
		return hl_attribs->background == DEFAULT_COLOR ? renderer->hl_attribs[0].background : hl_attribs->background;
	}
}

uint32_t CreateSpecialColor(Renderer *renderer, HighlightAttributes *hl_attribs) {
	return hl_attribs->special == DEFAULT_COLOR ? renderer->hl_attribs[0].special : hl_attribs->special;
}

void ApplyHighlightAttributes(Renderer *renderer, HighlightAttributes *hl_attribs,
	IDWriteTextLayout *text_layout, int start, int end) {
	GlyphDrawingEffect *drawing_effect = new GlyphDrawingEffect(
			CreateForegroundColor(renderer, hl_attribs),
			CreateSpecialColor(renderer, hl_attribs)
	);
	DWRITE_TEXT_RANGE range {
		.startPosition = static_cast<uint32_t>(start),
		.length = static_cast<uint32_t>(end - start)
	};
	if (hl_attribs->flags & HL_ATTRIB_ITALIC) {
		text_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_BOLD) {
		text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
		if(renderer->font_size != renderer->font_size_scale_bold)
			text_layout->SetFontSize(renderer->font_size_scale_bold, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_STRIKETHROUGH) {
		text_layout->SetStrikethrough(true, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_UNDERLINE) {
		text_layout->SetUnderline(true, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_UNDERCURL) {
		text_layout->SetUnderline(true, range);
	}
	text_layout->SetDrawingEffect(drawing_effect, range);
}

void DrawBackgroundRect(Renderer *renderer, D2D1_RECT_F rect, HighlightAttributes *hl_attribs) {
	uint32_t color = CreateBackgroundColor(renderer, hl_attribs);
	renderer->d2d_background_rect_brush->SetColor(D2D1::ColorF(color));

	renderer->d2d_context->FillRectangle(rect, renderer->d2d_background_rect_brush);
}

D2D1_RECT_F GetCursorForegroundRect(Renderer *renderer, D2D1_RECT_F cursor_bg_rect) {
	if (renderer->cursor.mode_info) {
		switch (renderer->cursor.mode_info->shape) {
		case CursorShape::None: {
		} return cursor_bg_rect;
		case CursorShape::Block: {
		} return cursor_bg_rect;
		case CursorShape::Vertical: {
			cursor_bg_rect.right = cursor_bg_rect.left + 2;
		} return cursor_bg_rect;
		case CursorShape::Horizontal: {
			cursor_bg_rect.top = cursor_bg_rect.bottom - 2;
		} return cursor_bg_rect;
		}
	}
	return cursor_bg_rect;
}

void DrawHighlightedText(Renderer *renderer, D2D1_RECT_F rect, wchar_t *text, uint32_t length, HighlightAttributes *hl_attribs) {
	IDWriteTextLayout *text_layout = nullptr;
	WIN_CHECK(renderer->dwrite_factory->CreateTextLayout(
		text,
		length,
		renderer->dwrite_text_format,
		rect.right - rect.left,
		rect.bottom - rect.top,
		&text_layout
	));
	ApplyHighlightAttributes(renderer, hl_attribs, text_layout, 0, 1);

	renderer->d2d_context->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
	text_layout->Draw(renderer, renderer->glyph_renderer, rect.left, rect.top);
	text_layout->Release();
	renderer->d2d_context->PopAxisAlignedClip();
}

void DrawGridLine(Renderer *renderer, int row) {
	int base = row * renderer->grid_cols;

	D2D1_RECT_F rect {
		.left = 0.0f,
		.top = row * renderer->font_height,
		.right = renderer->grid_cols * renderer->font_width,
		.bottom = (row * renderer->font_height) + renderer->font_height
	};

	IDWriteTextLayout *temp_text_layout = nullptr;
	WIN_CHECK(renderer->dwrite_factory->CreateTextLayout(
		&renderer->grid_chars[base],
		renderer->grid_cols,
		renderer->dwrite_text_format,
		rect.right - rect.left,
		rect.bottom - rect.top,
		&temp_text_layout
	));
	IDWriteTextLayout1 *text_layout;
	temp_text_layout->QueryInterface<IDWriteTextLayout1>(&text_layout);
	temp_text_layout->Release();

	uint16_t hl_attrib_id = renderer->grid_cell_properties[base].hl_attrib_id;
	int col_offset = 0;
	for (int i = 0; i < renderer->grid_cols; ++i) {
		// Add spacing for wide chars
		if (renderer->grid_cell_properties[base + i].is_wide_char) {
			float char_width = GetTextWidth(renderer, &renderer->grid_chars[base + i], 2);
			DWRITE_TEXT_RANGE range { .startPosition = static_cast<uint32_t>(i), .length = 1 };
			text_layout->SetCharacterSpacing(0, (renderer->font_width * 2) - char_width, 0, range);
		}

		// Add spacing for unicode chars. These characters are still single char width, 
		// but some of them by default will take up a bit more or less, leading to issues. 
		// So we realign them here.	
		else if(renderer->grid_chars[base + i] > 0xFF) {
			float char_width = GetTextWidth(renderer, &renderer->grid_chars[base + i], 1);
			if(abs(char_width - renderer->font_width) > 0.01f) {
				DWRITE_TEXT_RANGE range { .startPosition = static_cast<uint32_t>(i), .length = 1 };
				text_layout->SetCharacterSpacing(0, renderer->font_width - char_width, 0, range);
			}
		}
		else {
			// Add spacing for character not existing in this font
			uint16_t glyph_index;
			uint32_t code = static_cast<uint32_t>(renderer->grid_chars[base + i]);
			WIN_CHECK(renderer->font_face->GetGlyphIndicesW(&code, 1, &glyph_index));
			if (glyph_index == 0)
			{
				float char_width = GetTextWidth(renderer, &renderer->grid_chars[base + i], 1);
				float d_width = renderer->font_width - char_width;
				if (d_width > 0)
				{
					DWRITE_TEXT_RANGE range{ .startPosition = static_cast<uint32_t>(i), .length = 1 };
					text_layout->SetCharacterSpacing(d_width / 2, d_width / 2, 0, range);
				}
			}
		}

		// Check if the attributes change, 
		// if so draw until this point and continue with the new attributes
		if (renderer->grid_cell_properties[base + i].hl_attrib_id != hl_attrib_id) {
			D2D1_RECT_F bg_rect {
				.left = col_offset * renderer->font_width,
				.top = row * renderer->font_height,
				.right = col_offset * renderer->font_width + renderer->font_width * (i - col_offset),
				.bottom = (row * renderer->font_height) + renderer->font_height
			};
			DrawBackgroundRect(renderer, bg_rect, &renderer->hl_attribs[hl_attrib_id]);
			ApplyHighlightAttributes(renderer, &renderer->hl_attribs[hl_attrib_id], text_layout, col_offset, i);

			hl_attrib_id = renderer->grid_cell_properties[base + i].hl_attrib_id;
			col_offset = i;
		}
	}
	
	// Draw the remaining columns, there is always atleast the last column to draw,
	// but potentially more in case the last X columns share the same hl_attrib
	D2D1_RECT_F last_rect = rect;
	last_rect.left = col_offset * renderer->font_width;
	DrawBackgroundRect(renderer, last_rect, &renderer->hl_attribs[hl_attrib_id]);
	ApplyHighlightAttributes(renderer, &renderer->hl_attribs[hl_attrib_id], text_layout, col_offset, renderer->grid_cols);

	renderer->d2d_context->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
	if(renderer->disable_ligatures) {
		text_layout->SetTypography(renderer->dwrite_typography, DWRITE_TEXT_RANGE { 
			.startPosition = 0, 
			.length = static_cast<uint32_t>(renderer->grid_cols) 
		});
	}
	text_layout->Draw(renderer, renderer->glyph_renderer, 0.0f, rect.top);
	renderer->d2d_context->PopAxisAlignedClip();
	text_layout->Release();
}

void DrawAllGridLines(Renderer *renderer) {
	for (size_t i = 0; i < renderer->grid_rows; ++i) {
		DrawGridLine(renderer, i);
	}
}

bool IsSurrogatePair(wchar_t left, wchar_t right) {
	return (0xD800 <= left && left <= 0xDBFF) && (0xDC00 <= right && right <= 0xDFFF);
}

void DrawGridLines(Renderer *renderer, mpack_node_t grid_lines) {
	assert(renderer->grid_chars != nullptr);
	assert(renderer->grid_cell_properties != nullptr);
	
	int grid_size = renderer->grid_cols * renderer->grid_rows;
	size_t line_count = mpack_node_array_length(grid_lines);
	for (size_t i = 1; i < line_count; ++i) {
		mpack_node_t grid_line = mpack_node_array_at(grid_lines, i);

		int row = MPackIntFromArray(grid_line, 1);
		int col_start = MPackIntFromArray(grid_line, 2);

		mpack_node_t cell_array = mpack_node_array_at(grid_line, 3);
		size_t cell_array_length = mpack_node_array_length(cell_array);

		int hl_attrib_id = 0;
		int offset = row * renderer->grid_cols + col_start;
		for (size_t j = 0; j < cell_array_length; ++j) {
			mpack_node_t cell = mpack_node_array_at(cell_array, j);
			size_t cell_length = mpack_node_array_length(cell);

			mpack_node_t text = mpack_node_array_at(cell, 0);
			const char *str = mpack_node_str(text);

			if (cell_length > 1) {
				hl_attrib_id = MPackIntFromArray(cell, 1);
			}

			int repeat = 1;
			if (cell_length > 2) {
				repeat = MPackIntFromArray(cell, 2);
			}

			int strlen = static_cast<int>(mpack_node_strlen(text));
			if (strlen == 0) {
				// This is the right part of the wide char. Sadly grid_line
				// event can be splitted at the middle of wide character.

				// Be careful not to overwrite right half of surrogate pair.
				// It never happens that offset == 0, since it is the right
				// half of wide char, but add check for safety.
				if (offset == 0 || !IsSurrogatePair(renderer->grid_chars[offset - 1], renderer->grid_chars[offset])) {
					renderer->grid_chars[offset] = L'\0';
				}

				// This cell itself is not a wide character.
				renderer->grid_cell_properties[offset].is_wide_char = false;

				// Adjust properties. Again it never happens that offset == 0,
				// since it is the right half of wide char, but adding check
				// for safety.
				if (offset > 0) {
					// Set is_wide_char flag for the left cell to true.
					renderer->grid_cell_properties[offset - 1].is_wide_char = true;

					// Inherit hl_attrib_id from left half.
					renderer->grid_cell_properties[offset].hl_attrib_id = renderer->grid_cell_properties[offset - 1].hl_attrib_id;
				}

				++offset;
			} else {
				// This is single width character or left half cell of wide
				// character.

				// Left cell should not be a wide character, so reset the
				// flag. This time checking offset > 0 is mandatory.
				if (offset > 0) {
					renderer->grid_cell_properties[offset - 1].is_wide_char = false;
				}

				// Wide character will never be repeated, so we don't have to
				// handle wide character specially.
				for (int k = 0; k < repeat; ++k) {
					int wstrlen = MultiByteToWideChar(CP_UTF8, 0, str, strlen, &renderer->grid_chars[offset], grid_size - offset);
					if (wstrlen == 2) {
						// If the str takes two wchars, it must be a surrogate pair.
						bool is_surrogate_pair = IsSurrogatePair(renderer->grid_chars[offset], renderer->grid_chars[offset + 1]);
						assert(is_surrogate_pair && repeat == 1);
					}
					renderer->grid_cell_properties[offset].hl_attrib_id = hl_attrib_id;

					// Here we set is_wide_char to be always false. This is
					// because if it is actually a wide character, then the
					// right half of the char, empty string, should be appear
					// soon, and the flag will be set there (first branch of
					// this `if`).
					renderer->grid_cell_properties[offset].is_wide_char = false;

					++offset;
				}
			}
		}

		DrawGridLine(renderer, row);
	}
}

void DrawCursor(Renderer *renderer) {
	if (!renderer->cursor.mode_info) return;
	int cursor_grid_offset = renderer->cursor.row * renderer->grid_cols + renderer->cursor.col;

	int double_width_char_factor = 1;
	if (cursor_grid_offset < (renderer->grid_rows * renderer->grid_cols) &&
		renderer->grid_cell_properties[cursor_grid_offset].is_wide_char) {
		double_width_char_factor += 1;
	}

	HighlightAttributes cursor_hl_attribs = renderer->hl_attribs[renderer->cursor.mode_info->hl_attrib_id];

	// Inherit GUI options for char under cursor (like italic)
	int hl_attrib_id_under_cursor = renderer->grid_cell_properties[cursor_grid_offset].hl_attrib_id;
	HighlightAttributes under_cursor_hl_attribs = renderer->hl_attribs[hl_attrib_id_under_cursor];
	cursor_hl_attribs.flags = under_cursor_hl_attribs.flags;

	if (renderer->cursor.mode_info->hl_attrib_id == 0) {
		cursor_hl_attribs.flags ^= HL_ATTRIB_REVERSE;
	}

	D2D1_RECT_F cursor_rect {
		.left = renderer->cursor.col * renderer->font_width,
		.top = renderer->cursor.row * renderer->font_height,
		.right = renderer->cursor.col * renderer->font_width + renderer->font_width * double_width_char_factor,
		.bottom = (renderer->cursor.row * renderer->font_height) + renderer->font_height
	};
	D2D1_RECT_F cursor_fg_rect = GetCursorForegroundRect(renderer, cursor_rect);
	DrawBackgroundRect(renderer, cursor_fg_rect, &cursor_hl_attribs);

	if (renderer->cursor.mode_info->shape == CursorShape::Block) {
		DrawHighlightedText(renderer, cursor_fg_rect, &renderer->grid_chars[cursor_grid_offset], 
			double_width_char_factor, &cursor_hl_attribs);
	}
}

void UpdateGridSize(Renderer *renderer, mpack_node_t grid_resize) {
	mpack_node_t grid_resize_params = mpack_node_array_at(grid_resize, 1);
	int grid_cols = MPackIntFromArray(grid_resize_params, 1);
	int grid_rows = MPackIntFromArray(grid_resize_params, 2);

	if (renderer->grid_chars == nullptr ||
		renderer->grid_cell_properties == nullptr ||
		renderer->grid_cols != grid_cols ||
		renderer->grid_rows != grid_rows) {
		
		renderer->grid_cols = grid_cols;
		renderer->grid_rows = grid_rows;

		renderer->grid_chars = static_cast<wchar_t *>(malloc(static_cast<size_t>(grid_cols) * grid_rows * sizeof(wchar_t)));
		// Initialize all grid character to a space. An empty
		// grid cell is equivalent to a space in a text layout
		for (int i = 0; i < grid_cols * grid_rows; ++i) {
			renderer->grid_chars[i] = L' ';
		}
		renderer->grid_cell_properties = static_cast<CellProperty *>(calloc(static_cast<size_t>(grid_cols) * grid_rows, sizeof(CellProperty)));

		renderer->grid_initialized = true;
	}
}

void UpdateCursorPos(Renderer *renderer, mpack_node_t cursor_goto) {
	mpack_node_t cursor_goto_params = mpack_node_array_at(cursor_goto, 1);
	renderer->cursor.row = MPackIntFromArray(cursor_goto_params, 1);
	renderer->cursor.col = MPackIntFromArray(cursor_goto_params, 2);
}

void UpdateImePos(Renderer* renderer) {
	HIMC input_context = ImmGetContext(renderer->hwnd);
	COMPOSITIONFORM composition_form {
		.dwStyle = CFS_POINT,
		.ptCurrentPos = {
			.x = static_cast<LONG>(renderer->cursor.col * renderer->font_width),
			.y = static_cast<LONG>(renderer->cursor.row * renderer->font_height)
		}
	};

	if (ImmSetCompositionWindow(input_context, &composition_form)) {
		LOGFONTW font_attribs {
			.lfHeight = static_cast<LONG>(renderer->font_height)
		};
		wcscpy_s(font_attribs.lfFaceName, LF_FACESIZE, renderer->font);
		ImmSetCompositionFontW(input_context, &font_attribs);
	}

	ImmReleaseContext(renderer->hwnd, input_context);
}

void UpdateWindowTitle(Renderer *renderer, mpack_node_t set_title) {
	// Get new title
	mpack_node_t params = mpack_node_array_at(set_title, 1);
	mpack_node_t value = mpack_node_array_at(params, 0);
	const char *new_title = mpack_node_str(value);
	int len = mpack_node_strlen(value);

	// Append " - Nvy" to the title. If title is empty, do not add " - ".
	const char *append = len == 0 ? "Nvy" : " - Nvy";
	size_t add_len = strlen(append);
	size_t bytes = len + add_len; // No need for '\0'
	char *buf = static_cast<char *>(malloc(bytes));
	memcpy(buf, new_title, len);
	memcpy(buf + len, append, add_len);

	// Convert to wide string
	int wstrlen = MultiByteToWideChar(CP_UTF8, 0, buf, len + add_len, NULL, 0);
	wchar_t *wbuf = static_cast<wchar_t *>(malloc((wstrlen + 1) * sizeof(wchar_t)));
	MultiByteToWideChar(CP_UTF8, 0, buf, len + add_len, wbuf, wstrlen);
	wbuf[wstrlen] = '\0';

	// Update title bar text
	SetWindowText(renderer->hwnd, wbuf);

	free(buf);
	free(wbuf);
}

void UpdateCursorMode(Renderer *renderer, mpack_node_t mode_change) {
	mpack_node_t mode_change_params = mpack_node_array_at(mode_change, 1);
	renderer->cursor.mode_info = &renderer->cursor_mode_infos[mpack_node_array_at(mode_change_params, 1).data->value.u];
}

void UpdateCursorModeInfos(Renderer *renderer, mpack_node_t mode_info_set_params) {
	mpack_node_t mode_info_params = mpack_node_array_at(mode_info_set_params, 1);
	mpack_node_t mode_infos = mpack_node_array_at(mode_info_params, 1);
	size_t mode_infos_length = mpack_node_array_length(mode_infos);
	assert(mode_infos_length <= MAX_CURSOR_MODE_INFOS);

	for (size_t i = 0; i < mode_infos_length; ++i) {
		mpack_node_t mode_info_map = mpack_node_array_at(mode_infos, i);

		renderer->cursor_mode_infos[i].shape = CursorShape::None;
		mpack_node_t cursor_shape = mpack_node_map_cstr_optional(mode_info_map, "cursor_shape");
		if (!mpack_node_is_missing(cursor_shape)) {
			const char *cursor_shape_str = mpack_node_str(cursor_shape);
			size_t strlen = mpack_node_strlen(cursor_shape);
			if (!strncmp(cursor_shape_str, "block", strlen)) {
				renderer->cursor_mode_infos[i].shape = CursorShape::Block;
			}
			else if (!strncmp(cursor_shape_str, "vertical", strlen)) {
				renderer->cursor_mode_infos[i].shape = CursorShape::Vertical;
			}
			else if (!strncmp(cursor_shape_str, "horizontal", strlen)) {
				renderer->cursor_mode_infos[i].shape = CursorShape::Horizontal;
			}
		}

		renderer->cursor_mode_infos[i].hl_attrib_id = 0;
		mpack_node_t hl_attrib_index = mpack_node_map_cstr_optional(mode_info_map, "attr_id");
		if (!mpack_node_is_missing(hl_attrib_index)) {
			renderer->cursor_mode_infos[i].hl_attrib_id = static_cast<int>(hl_attrib_index.data->value.i);
		}
	}
}

void ScrollRegion(Renderer *renderer, mpack_node_t scroll_region) {
	mpack_node_t scroll_region_params = mpack_node_array_at(scroll_region, 1);

	int64_t top = mpack_node_array_at(scroll_region_params, 1).data->value.i;
	int64_t bottom = mpack_node_array_at(scroll_region_params, 2).data->value.i;
	int64_t left = mpack_node_array_at(scroll_region_params, 3).data->value.i;
	int64_t right = mpack_node_array_at(scroll_region_params, 4).data->value.i;
	int64_t rows = mpack_node_array_at(scroll_region_params, 5).data->value.i;
	int64_t cols = mpack_node_array_at(scroll_region_params, 6).data->value.i;

	// Currently nvim does not support horizontal scrolling, 
	// the parameter is reserved for later use
	assert(cols == 0);

	// This part is slightly cryptic, basically we're just
	// iterating from top to bottom or vice versa depending on scroll direction.
	bool scrolling_down = rows > 0;
	int64_t start_row = scrolling_down ? top : bottom - 1;
	int64_t end_row = scrolling_down ? bottom - 1 : top;
	int64_t increment = scrolling_down ? 1 : -1;

	for (int64_t i = start_row; scrolling_down ? i <= end_row : i >= end_row; i += increment) {
		// Clip anything outside the scroll region
		int64_t target_row = i - rows;
		if (target_row < top || target_row >= bottom) {
			continue;
		}

		memcpy(
			&renderer->grid_chars[target_row * renderer->grid_cols + left],
			&renderer->grid_chars[i * renderer->grid_cols + left],
			(right - left) * sizeof(wchar_t)
		);

		memcpy(
			&renderer->grid_cell_properties[target_row * renderer->grid_cols + left],
			&renderer->grid_cell_properties[i * renderer->grid_cols + left],
			(right - left) * sizeof(CellProperty)
		);

		// Sadly I have given up on making use of IDXGISwapChain1::Present1
		// scroll_rects or bitmap copies. The former seems insufficient for
		// nvim since it can require multiple scrolls per frame, the latter
		// I can't seem to make work with the FLIP_SEQUENTIAL swapchain model.
		// Thus we fall back to drawing the appropriate scrolled grid lines
		DrawGridLine(renderer, target_row);
	}

	// Redraw the line which the cursor has moved to, as it is no
	// longer guaranteed that the cursor is still there
	int cursor_row = renderer->cursor.row - rows;
	if(cursor_row >= 0 && cursor_row < renderer->grid_rows) {
		DrawGridLine(renderer, cursor_row);
	}
}

void DrawBorderRectangles(Renderer *renderer) {
	float left_border = renderer->font_width * renderer->grid_cols;
	float top_border = renderer->font_height * renderer->grid_rows;

	if(left_border != static_cast<float>(renderer->pixel_size.width)) {
		D2D1_RECT_F vertical_rect {
			.left = left_border,
			.top = 0.0f,
			.right = static_cast<float>(renderer->pixel_size.width),
			.bottom = static_cast<float>(renderer->pixel_size.height)
		};
		DrawBackgroundRect(renderer, vertical_rect, &renderer->hl_attribs[0]);
	}

	if(top_border != static_cast<float>(renderer->pixel_size.height)) {
		D2D1_RECT_F horizontal_rect {
			.left = 0.0f,
			.top = top_border,
			.right = static_cast<float>(renderer->pixel_size.width),
			.bottom = static_cast<float>(renderer->pixel_size.height)
		};
		DrawBackgroundRect(renderer, horizontal_rect, &renderer->hl_attribs[0]);
	}
}

bool RendererUpdateGuiFont(Renderer *renderer, const char *guifont, size_t strlen) {
	if (strlen == 0) {
		return false;
	}

	const char *size_str = strstr(guifont, ":h");
	if (!size_str) {
		return false;
	}

	size_t font_str_len = size_str - guifont;
	size_t size_str_len = strlen - (font_str_len + 2);
	size_str += 2;

	const char *fallback_font_str = strstr(size_str, ":");
	if(fallback_font_str) {
		fallback_font_str += 1;
		size_t fallback_font_str_len = strlen - (fallback_font_str - guifont);

		int wstrlen = MultiByteToWideChar(CP_UTF8, 0, fallback_font_str, fallback_font_str_len, 0, 0);
		if (wstrlen != 0 && wstrlen < MAX_FONT_LENGTH) {
			MultiByteToWideChar(CP_UTF8, 0, fallback_font_str, fallback_font_str_len, renderer->fallback_font, MAX_FONT_LENGTH - 1);
			renderer->fallback_font[wstrlen] = L'\0';
		}

		size_str_len -= fallback_font_str_len;
	}

	float font_size = DEFAULT_FONT_SIZE;
	// Assume font size part of string is less than 256 characters
	if(size_str_len < 256) {
		char font_size_str[256];
		memcpy(font_size_str, size_str, size_str_len);
		font_size_str[size_str_len] = '\0';
		font_size = static_cast<float>(atof(font_size_str));
	}

	return RendererUpdateFont(renderer, font_size, guifont, static_cast<int>(font_str_len));
}

void SetGuiOptions(Renderer *renderer, mpack_node_t option_set) {
	uint64_t option_set_length = mpack_node_array_length(option_set);

	for (uint64_t i = 1; i < option_set_length; ++i) {
		mpack_node_t name = mpack_node_array_at(mpack_node_array_at(option_set, i), 0);
		mpack_node_t value = mpack_node_array_at(mpack_node_array_at(option_set, i), 1);
		if (MPackMatchString(name, "guifont")) {
			const char *font_str = mpack_node_str(value);
			size_t strlen = mpack_node_strlen(value);
			RendererUpdateGuiFont(renderer, font_str, strlen);

			// Send message to window in order to update nvim row/col count
			PostMessage(renderer->hwnd, WM_RENDERER_FONT_UPDATE, 0, 0);
		}
	}
}

void ClearGrid(Renderer *renderer) {
	// Initialize all grid character to a space.
	for (int i = 0; i < renderer->grid_cols * renderer->grid_rows; ++i) {
		renderer->grid_chars[i] = L' ';
	}
	memset(renderer->grid_cell_properties, 0, renderer->grid_cols * renderer->grid_rows * sizeof(CellProperty));
	D2D1_RECT_F rect {
		.left = 0.0f,
		.top = 0.0f,
		.right = renderer->grid_cols * renderer->font_width,
		.bottom = renderer->grid_rows * renderer->font_height
	};
	DrawBackgroundRect(renderer, rect, &renderer->hl_attribs[0]);
}

void StartDraw(Renderer *renderer) {
	if (!renderer->draw_active) {
		WaitForSingleObjectEx(
			renderer->swapchain_wait_handle,
			1000,
			true
		);

		renderer->d2d_context->SetTarget(renderer->d2d_target_bitmap);
		renderer->d2d_context->BeginDraw();
		renderer->d2d_context->SetTransform(D2D1::IdentityMatrix());
		renderer->draw_active = true;
	}
}

void CopyFrontToBack(Renderer *renderer) {
	ID3D11Resource *front;
	ID3D11Resource *back;
	WIN_CHECK(renderer->dxgi_swapchain->GetBuffer(0, IID_PPV_ARGS(&back)));
	WIN_CHECK(renderer->dxgi_swapchain->GetBuffer(1, IID_PPV_ARGS(&front)));
	renderer->d3d_context->CopyResource(back, front);

	SafeRelease(&front);
	SafeRelease(&back);
}

void FinishDraw(Renderer *renderer) {
	renderer->d2d_context->EndDraw();

	HRESULT hr = renderer->dxgi_swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	renderer->draw_active = false;

	CopyFrontToBack(renderer);

	if (hr == DXGI_ERROR_DEVICE_REMOVED) {
		HandleDeviceLost(renderer);
	}
}

void RendererRedraw(Renderer *renderer, mpack_node_t params, bool start_maximized) {
	StartDraw(renderer);

	uint64_t redraw_commands_length = mpack_node_array_length(params);
	for (uint64_t i = 0; i < redraw_commands_length; ++i) {
		mpack_node_t redraw_command_arr = mpack_node_array_at(params, i);
		mpack_node_t redraw_command_name = mpack_node_array_at(redraw_command_arr, 0);

		if (MPackMatchString(redraw_command_name, "option_set")) {
			SetGuiOptions(renderer, redraw_command_arr);
		}
		if (MPackMatchString(redraw_command_name, "grid_resize")) {
			UpdateGridSize(renderer, redraw_command_arr);
		}
		if (MPackMatchString(redraw_command_name, "grid_clear")) {
			ClearGrid(renderer);
		}
		else if (MPackMatchString(redraw_command_name, "default_colors_set")) {
			UpdateDefaultColors(renderer, redraw_command_arr);
			renderer->draws_invalidated = true;
		}
		else if (MPackMatchString(redraw_command_name, "hl_attr_define")) {
			UpdateHighlightAttributes(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_line")) {
			DrawGridLines(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_cursor_goto")) {
			// If the old cursor position is still within the row bounds,
			// redraw the line to get rid of the cursor
			if(renderer->cursor.row < renderer->grid_rows) {
				DrawGridLine(renderer, renderer->cursor.row);
			}
			UpdateCursorPos(renderer, redraw_command_arr);
			UpdateImePos(renderer);
		}
		else if (MPackMatchString(redraw_command_name, "mode_info_set")) {
			UpdateCursorModeInfos(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "mode_change")) {
			// Redraw cursor if its inside the bounds
			if(renderer->cursor.row < renderer->grid_rows) {
				DrawGridLine(renderer, renderer->cursor.row);
			}
			UpdateCursorMode(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "set_title")) {
			UpdateWindowTitle(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "busy_start")) {
			renderer->ui_busy = true;
			// Hide cursor while UI is busy
			if(renderer->cursor.row < renderer->grid_rows) {
				DrawGridLine(renderer, renderer->cursor.row);
			}
		}
		else if (MPackMatchString(redraw_command_name, "busy_stop")) {
			renderer->ui_busy = false;
		}
		else if (MPackMatchString(redraw_command_name, "grid_scroll")) {
			ScrollRegion(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "flush")) {
			if (!renderer->has_drawn) {
				renderer->has_drawn = true;
				ShowWindow(renderer->hwnd, start_maximized ? SW_MAXIMIZE : SW_SHOWDEFAULT);			}

			if (renderer->draws_invalidated) {
				renderer->draws_invalidated = false;
				DrawAllGridLines(renderer);
			}

			if(!renderer->ui_busy) {
				DrawCursor(renderer);
			}
			DrawBorderRectangles(renderer);
			FinishDraw(renderer);
		}
	}
}

PixelSize RendererGridToPixelSize(Renderer *renderer, int rows, int cols) {
	int requested_width = static_cast<int>(ceilf(renderer->font_width) * cols);
	int requested_height = static_cast<int>(ceilf(renderer->font_height) * rows);

	// Adjust size to include title bar
	RECT adjusted_rect = { 0, 0, requested_width, requested_height };
	AdjustWindowRect(&adjusted_rect, WS_OVERLAPPEDWINDOW, false);
	return PixelSize {
		.width = adjusted_rect.right - adjusted_rect.left,
		.height = adjusted_rect.bottom - adjusted_rect.top
	};
}

GridSize RendererPixelsToGridSize(Renderer *renderer, int width, int height) {
	return GridSize {
		.rows = static_cast<int>(height / renderer->font_height),
		.cols = static_cast<int>(width / renderer->font_width)
	};
}

GridPoint RendererCursorToGridPoint(Renderer *renderer, int x, int y) {
	return GridPoint {
		.row = static_cast<int>(y / renderer->font_height),
		.col = static_cast<int>(x / renderer->font_width)
	};
}
