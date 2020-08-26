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
	renderer->d2d_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

	SafeRelease(&dxgi_device);
}

void InitializeDWrite(Renderer *renderer) {
	WIN_CHECK(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory4), reinterpret_cast<IUnknown **>(&renderer->dwrite_factory)));
}

void HandleDeviceLost(Renderer *renderer);
void InitializeWindowDependentResources(Renderer *renderer, uint32_t width, uint32_t height) {
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
			0
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
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE
		};

		IDXGIDevice3 *dxgi_device;
		WIN_CHECK(renderer->d3d_device->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void **>(&dxgi_device)));
		IDXGIAdapter *dxgi_adapter;
		WIN_CHECK(dxgi_device->GetAdapter(&dxgi_adapter));
		IDXGIFactory2 *dxgi_factory;
		WIN_CHECK(dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory)));

		WIN_CHECK(dxgi_factory->CreateSwapChainForHwnd(renderer->d3d_device,
			renderer->hwnd, &swapchain_desc, nullptr, nullptr, &renderer->dxgi_swapchain));
		WIN_CHECK(dxgi_factory->MakeWindowAssociation(renderer->hwnd, DXGI_MWA_NO_ALT_ENTER));
		WIN_CHECK(dxgi_device->SetMaximumFrameLatency(1));
		
		SafeRelease(&dxgi_device);
		SafeRelease(&dxgi_adapter);
		SafeRelease(&dxgi_factory);
	}

	constexpr D2D1_BITMAP_PROPERTIES1 target_bitmap_properties {
		.pixelFormat = D2D1_PIXEL_FORMAT {
			.format = DXGI_FORMAT_B8G8R8A8_UNORM,
			.alphaMode = D2D1_ALPHA_MODE_IGNORE
		},
		.dpiX = 96.0f,
		.dpiY = 96.0f,
		.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW
	};
	IDXGISurface2 *dxgi_backbuffer;
	WIN_CHECK(renderer->dxgi_swapchain->GetBuffer(0, IID_PPV_ARGS(&dxgi_backbuffer)));
	WIN_CHECK(renderer->d2d_context->CreateBitmapFromDxgiSurface(
		dxgi_backbuffer,
		&target_bitmap_properties,
		&renderer->d2d_target_bitmap
	));
	renderer->initial_draw = true;

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

void RendererInitialize(Renderer *renderer, HWND hwnd, const char *font, float font_size) {
	renderer->hwnd = hwnd;
	renderer->dpi_scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;

    renderer->hl_attribs.resize(MAX_HIGHLIGHT_ATTRIBS);

	InitializeD2D(renderer);
	InitializeD3D(renderer);
	InitializeDWrite(renderer);
	renderer->glyph_renderer = new GlyphRenderer(renderer);

	RECT client_rect;
	GetClientRect(hwnd, &client_rect);
	InitializeWindowDependentResources(
		renderer,
		static_cast<uint32_t>(client_rect.right - client_rect.left),
		static_cast<uint32_t>(client_rect.bottom - client_rect.top)
	);

	RendererUpdateFont(renderer, font_size, font, static_cast<int>(strlen(font)));
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

void UpdateFontSize(Renderer *renderer, float font_size) {
	renderer->font_size = font_size * renderer->dpi_scale;

	WIN_CHECK(renderer->dwrite_factory->CreateTextFormat(
		renderer->font,
		nullptr,
		DWRITE_FONT_WEIGHT_MEDIUM,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		renderer->font_size,
		L"en-us",
		&renderer->dwrite_text_format
	));

	// Update the width based on a hit test
	wchar_t character = L'A';
	renderer->font_width = GetTextWidth(renderer, &character, 1);

	// We calculate the height and the baseline from the font metrics, 
	// and ensure uniform line spacing. This ensures that double-width
	// characters and characters using a fallback font stay on the line
	int font_height_em = renderer->font_metrics.ascent + renderer->font_metrics.descent + renderer->font_metrics.lineGap;
	renderer->font_height = (static_cast<float>(font_height_em) * renderer->font_size) /
		static_cast<float>(renderer->font_metrics.designUnitsPerEm);
	renderer->font_ascent = (static_cast<float>(renderer->font_metrics.ascent) * renderer->font_size) /
		static_cast<float>(renderer->font_metrics.designUnitsPerEm);

	WIN_CHECK(renderer->dwrite_text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
	WIN_CHECK(renderer->dwrite_text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

	float baseline = (static_cast<float>(renderer->font_metrics.ascent) * renderer->font_size) /
		static_cast<float>(renderer->font_metrics.designUnitsPerEm);

	renderer->line_spacing = ceilf(renderer->font_height);
	WIN_CHECK(renderer->dwrite_text_format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, renderer->line_spacing, baseline));
}

void UpdateFontMetrics(Renderer *renderer, const char* font_string, int strlen) {
	if (strlen == 0) {
		return;
	}

	IDWriteFontCollection *font_collection;
	WIN_CHECK(renderer->dwrite_factory->GetSystemFontCollection(&font_collection));

	int wstrlen = MultiByteToWideChar(CP_UTF8, 0, font_string, strlen, 0, 0);

	if (wstrlen < MAX_FONT_LENGTH) {
		MultiByteToWideChar(CP_UTF8, 0, font_string, strlen, renderer->font, MAX_FONT_LENGTH - 1);
		renderer->font[wstrlen] = L'\0';
	}

	uint32_t index;
	BOOL exists;
	font_collection->FindFamilyName(renderer->font, &index, &exists);

    const wchar_t *fallback_font = L"Consolas";
	if (!exists) {
		font_collection->FindFamilyName(fallback_font, &index, &exists);
        memcpy(renderer->font, fallback_font, (wcslen(fallback_font) + 1) * sizeof(wchar_t));
	}

	IDWriteFontFamily *font_family;
	font_collection->GetFontFamily(index, &font_family);

	IDWriteFont *write_font;
	font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &write_font);

	IDWriteFont1 *write_font_1;
	write_font->QueryInterface<IDWriteFont1>(&write_font_1);
	write_font_1->GetMetrics(&renderer->font_metrics);

	write_font_1->Release();
	write_font->Release();
	font_family->Release();
	font_collection->Release();
}

void RendererUpdateFont(Renderer *renderer, float font_size, const char *font_string, int strlen) {
    // Arbitrary cut-off points for font-size
	if (font_size > 150.0f || font_size < 5.0f) {
		return;
	}

	if (renderer->dwrite_text_format) {
		renderer->dwrite_text_format->Release();
	}
	UpdateFontMetrics(renderer, font_string, strlen);
	UpdateFontSize(renderer, font_size);
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

void ApplyHighlightAttributes(Renderer *renderer, HighlightAttributes *hl_attribs,
	IDWriteTextLayout *text_layout, int start, int end) {
	GlyphDrawingEffect *drawing_effect = new GlyphDrawingEffect(CreateForegroundColor(renderer, hl_attribs));
	DWRITE_TEXT_RANGE range {
		.startPosition = static_cast<uint32_t>(start),
		.length = static_cast<uint32_t>(end - start)
	};
	if (hl_attribs->flags & HL_ATTRIB_ITALIC) {
		text_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_BOLD) {
		text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_STRIKETHROUGH) {
		text_layout->SetStrikethrough(true, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_UNDERLINE) {
		text_layout->SetUnderline(true, range);
	}
	if (hl_attribs->flags & HL_ATTRIB_UNDERCURL) {
		text_layout->SetUnderline(true, range);
		drawing_effect->undercurl = true;
	}
	text_layout->SetDrawingEffect(drawing_effect, range);
}

void DrawBackgroundRect(Renderer *renderer, D2D1_RECT_F rect, HighlightAttributes *hl_attribs) {
	uint32_t color = CreateBackgroundColor(renderer, hl_attribs);
	renderer->d2d_background_rect_brush->SetColor(D2D1::ColorF(color));

	renderer->d2d_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	renderer->d2d_context->FillRectangle(rect, renderer->d2d_background_rect_brush);
	renderer->d2d_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
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

inline RECT ToConstrainedRect(Renderer *renderer, D2D1_RECT_F *rect) {
	LONG rect_left = static_cast<LONG>(roundf((*rect).left));
	LONG rect_top = static_cast<LONG>(roundf((*rect).top));
	LONG rect_right = static_cast<LONG>(roundf((*rect).right));
	LONG rect_bottom = static_cast<LONG>(roundf((*rect).bottom));

	return RECT {
		.left = max(0, min(rect_left, static_cast<LONG>(renderer->pixel_size.width))),
		.top = max(0, min(rect_top, static_cast<LONG>(renderer->pixel_size.height))),
		.right = max(0, min(rect_right, static_cast<LONG>(renderer->pixel_size.width))),
		.bottom = max(0, min(rect_bottom, static_cast<LONG>(renderer->pixel_size.height))),
	};
}

void AddDirtyRect(Renderer *renderer, D2D1_RECT_F *rect) {
	RECT constrained_rect = ToConstrainedRect(renderer, rect);
	if (constrained_rect.left != constrained_rect.right &&
		constrained_rect.top != constrained_rect.bottom) {
		renderer->dirty_rects.push_back(constrained_rect);
	}
}

void DrawGridLine(Renderer *renderer, int row, int start, int end) {
	int base = row * renderer->grid_cols + start;

	D2D1_RECT_F rect {
		.left = start * renderer->font_width,
		.top = row * renderer->line_spacing,
		.right = end * renderer->font_width,
		.bottom = (row * renderer->line_spacing) + renderer->line_spacing
	};

	IDWriteTextLayout *temp_text_layout = nullptr;
	WIN_CHECK(renderer->dwrite_factory->CreateTextLayout(
		&renderer->grid_chars[base],
		end - start,
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
	for (int i = 0; i < (end - start); ++i) {
		if (renderer->grid_cell_properties[base + i].is_wide_char) {
			float char_width = GetTextWidth(renderer, &renderer->grid_chars[base + i], 2);
			DWRITE_TEXT_RANGE range { .startPosition = static_cast<uint32_t>(i), .length = 1 };
			text_layout->SetCharacterSpacing(0, (renderer->font_width * 2) - char_width, 0, range);
		}

		// Check if the attributes change, 
		// if so draw until this point and continue with the new attributes
		if (renderer->grid_cell_properties[base + i].hl_attrib_id != hl_attrib_id) {
			D2D1_RECT_F bg_rect {
				.left = (start + col_offset) * renderer->font_width,
				.top = row * renderer->line_spacing,
				.right = (start + col_offset) * renderer->font_width + renderer->font_width * (i - col_offset),
				.bottom = (row * renderer->line_spacing) + renderer->line_spacing
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
	last_rect.left = (start + col_offset) * renderer->font_width;
	DrawBackgroundRect(renderer, last_rect, &renderer->hl_attribs[hl_attrib_id]);
	ApplyHighlightAttributes(renderer, &renderer->hl_attribs[hl_attrib_id], text_layout, col_offset, end);

	renderer->d2d_context->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
	text_layout->Draw(renderer, renderer->glyph_renderer, start * renderer->font_width, rect.top);
	renderer->d2d_context->PopAxisAlignedClip();
	text_layout->Release();

	// Mark line rect dirty for redraw
	AddDirtyRect(renderer, &rect);
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

		mpack_node_t cells_array = mpack_node_array_at(grid_line, 3);
		size_t cells_array_length = mpack_node_array_length(cells_array);

		int col_offset = col_start;
		int hl_attrib_id = 0;
		for (size_t j = 0; j < cells_array_length; ++j) {

			mpack_node_t cells = mpack_node_array_at(cells_array, j);
			size_t cells_length = mpack_node_array_length(cells);

			mpack_node_t text = mpack_node_array_at(cells, 0);
			const char *str = mpack_node_str(text);

			int strlen = static_cast<int>(mpack_node_strlen(text));

			if (cells_length > 1) {
				hl_attrib_id = MPackIntFromArray(cells, 1);
			}

			// Right part of double-width char is the empty string, thus
			// if the next cell array contains the empty string, we can process
			// the current string as a double-width char and proceed
			if(j < (cells_array_length - 1) && 
				mpack_node_strlen(mpack_node_array_at(mpack_node_array_at(cells_array, j + 1), 0)) == 0) {

				int offset = row * renderer->grid_cols + col_offset;
				renderer->grid_cell_properties[offset].is_wide_char = true;
				renderer->grid_cell_properties[offset].hl_attrib_id = hl_attrib_id;
				renderer->grid_cell_properties[offset + 1].hl_attrib_id = hl_attrib_id;

				int wstrlen = MultiByteToWideChar(CP_UTF8, 0, str, strlen, &renderer->grid_chars[offset], grid_size - offset);
				assert(wstrlen == 1 || wstrlen == 2);

				if (wstrlen == 1) {
					renderer->grid_chars[offset + 1] = L'\0';
				}

				col_offset += 2;
				continue;
			}

			if (strlen == 0) {
				continue;
			}

			int repeat = 1;
			if (cells_length > 2) {
				repeat = MPackIntFromArray(cells, 2);
			}

			int offset = row * renderer->grid_cols + col_offset;
			int wstrlen = 0;
			for (int k = 0; k < repeat; ++k) {
				int idx = offset + (k * wstrlen);
				wstrlen = MultiByteToWideChar(CP_UTF8, 0, str, strlen, &renderer->grid_chars[idx], grid_size - idx);
			}

			int wstrlen_with_repetitions = wstrlen * repeat;
			for (int k = 0; k < wstrlen_with_repetitions; ++k) {
				renderer->grid_cell_properties[offset + k].hl_attrib_id = hl_attrib_id;
				renderer->grid_cell_properties[offset + k].is_wide_char = false;
			}

			col_offset += wstrlen_with_repetitions;
		}

		DrawGridLine(renderer, row, col_start, col_offset);
	}
}

void DrawCursor(Renderer *renderer) {
	int cursor_grid_offset = renderer->cursor.row * renderer->grid_cols + renderer->cursor.col;

	int double_width_char_factor = 1;
	if (cursor_grid_offset < (renderer->grid_rows * renderer->grid_cols) &&
		renderer->grid_cell_properties[cursor_grid_offset].is_wide_char) {
		double_width_char_factor += 1;
	}

	HighlightAttributes cursor_hl_attribs = renderer->hl_attribs[renderer->cursor.mode_info->hl_attrib_index];
	if (renderer->cursor.mode_info->hl_attrib_index == 0) {
		cursor_hl_attribs.flags ^= HL_ATTRIB_REVERSE;
	}

	D2D1_RECT_F cursor_rect {
		.left = renderer->cursor.col * renderer->font_width,
		.top = renderer->cursor.row * renderer->line_spacing,
		.right = renderer->cursor.col * renderer->font_width + renderer->font_width * double_width_char_factor,
		.bottom = (renderer->cursor.row * renderer->line_spacing) + renderer->line_spacing
	};
	D2D1_RECT_F cursor_fg_rect = GetCursorForegroundRect(renderer, cursor_rect);
	DrawBackgroundRect(renderer, cursor_fg_rect, &cursor_hl_attribs);

	if (renderer->cursor.mode_info->shape == CursorShape::Block) {
		DrawHighlightedText(renderer, cursor_fg_rect, &renderer->grid_chars[cursor_grid_offset], 
			double_width_char_factor, &cursor_hl_attribs);
	}
	AddDirtyRect(renderer, &cursor_fg_rect);
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
	}
}

void UpdateCursorPos(Renderer *renderer, mpack_node_t cursor_goto) {
	mpack_node_t cursor_goto_params = mpack_node_array_at(cursor_goto, 1);
	renderer->cursor.row = MPackIntFromArray(cursor_goto_params, 1);
	renderer->cursor.col = MPackIntFromArray(cursor_goto_params, 2);
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

		renderer->cursor_mode_infos[i].cell_percentage = 0;
		mpack_node_t cell_percentage = mpack_node_map_cstr_optional(mode_info_map, "cell_percentage");
		if (!mpack_node_is_missing(cell_percentage)) {
			renderer->cursor_mode_infos[i].cell_percentage = static_cast<float>(cell_percentage.data->value.i) / 100.0f;
		}

		renderer->cursor_mode_infos[i].hl_attrib_index = 0;
		mpack_node_t hl_attrib_index = mpack_node_map_cstr_optional(mode_info_map, "attr_id");
		if (!mpack_node_is_missing(hl_attrib_index)) {
			renderer->cursor_mode_infos[i].hl_attrib_index = static_cast<int>(hl_attrib_index.data->value.i);
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
	}

	renderer->scrolled_rect = RECT {
		.left = static_cast<LONG>(roundf(left * renderer->font_width)),
		.top = static_cast<LONG>(top * renderer->line_spacing),
		.right = static_cast<LONG>(roundf(right * renderer->font_width)),
		.bottom = static_cast<LONG>(bottom * renderer->line_spacing)
	};
	if (scrolling_down) {
		renderer->scrolled_rect.bottom = min(
			renderer->scrolled_rect.bottom,
			renderer->scrolled_rect.bottom - static_cast<LONG>(rows * renderer->line_spacing)
		);
	}
	else {
		renderer->scrolled_rect.top = max(
			renderer->scrolled_rect.top,
			renderer->scrolled_rect.top - static_cast<LONG>(rows * renderer->line_spacing)
		);
	}
	renderer->scroll_offset = POINT {
		.x = 0,
		.y =-static_cast<LONG>(rows * renderer->line_spacing)
	};
	renderer->scrolled = true;

    // Redraw the line which the cursor has moved to, as it is no
    // longer guaranteed that the cursor is still there
    int cursor_row = renderer->cursor.row - rows;
    if(cursor_row >= 0 && cursor_row < renderer->grid_rows) {
        DrawGridLine(renderer, cursor_row, 0, renderer->grid_cols);
    }
}

void DrawBorderRectangles(Renderer *renderer) {
	float left_border = roundf(renderer->font_width * renderer->grid_cols);
	float top_border = renderer->line_spacing * renderer->grid_rows;

    if(left_border != static_cast<float>(renderer->pixel_size.width)) {
        D2D1_RECT_F vertical_rect {
            .left = left_border,
            .top = 0.0f,
            .right = static_cast<float>(renderer->pixel_size.width),
            .bottom = static_cast<float>(renderer->pixel_size.height)
        };
        DrawBackgroundRect(renderer, vertical_rect, &renderer->hl_attribs[0]);
		AddDirtyRect(renderer, &vertical_rect);
    }

    if(top_border != static_cast<float>(renderer->pixel_size.height)) {
        D2D1_RECT_F horizontal_rect {
            .left = 0.0f,
            .top = top_border,
            .right = static_cast<float>(renderer->pixel_size.width),
            .bottom = static_cast<float>(renderer->pixel_size.height)
        };
        DrawBackgroundRect(renderer, horizontal_rect, &renderer->hl_attribs[0]);
		AddDirtyRect(renderer, &horizontal_rect);
    }
}

void SetGuiOptions(Renderer *renderer, mpack_node_t option_set) {
	uint64_t option_set_length = mpack_node_array_length(option_set);

	for (uint64_t i = 1; i < option_set_length; ++i) {
		mpack_node_t name = mpack_node_array_at(mpack_node_array_at(option_set, i), 0);
		mpack_node_t value = mpack_node_array_at(mpack_node_array_at(option_set, i), 1);
		if (MPackMatchString(name, "guifont")) {
			size_t strlen = mpack_node_strlen(value);
            if (strlen == 0) {
				continue;
			}

			const char *font_str = mpack_node_str(value);
			const char *size_str = strstr(font_str, ":h");
			if (!size_str) {
				continue;
			}

			size_t font_str_len = size_str - font_str;
			size_t size_str_len = strlen - (font_str_len + 2);
			size_str += 2;
			
			assert(size_str_len < 64);
			char font_size[64];
			memcpy(font_size, size_str, size_str_len);
			font_size[size_str_len] = '\0';

			RendererUpdateFont(renderer, static_cast<float>(atof(font_size)), font_str, static_cast<int>(font_str_len));
			// Send message to window in order to update nvim row/col count
			PostMessage(renderer->hwnd, WM_RENDERER_FONT_UPDATE, 0, 0);
		}
	}
}

void ClearGrid(Renderer *renderer) {
	D2D1_RECT_F rect {
		.left = 0.0f,
		.top = 0.0f,
		.right = renderer->grid_cols * renderer->font_width,
		.bottom = renderer->grid_rows * renderer->line_spacing
	};
	DrawBackgroundRect(renderer, rect, &renderer->hl_attribs[0]);
	AddDirtyRect(renderer, &rect);
}

void StartDraw(Renderer *renderer) {
	if (!renderer->draw_active) {
		renderer->d2d_context->SetTarget(renderer->d2d_target_bitmap);
		renderer->d2d_context->BeginDraw();
		renderer->d2d_context->SetTransform(D2D1::IdentityMatrix());
		renderer->draw_active = true;
	}
}

void FinishDraw(Renderer *renderer) {
	renderer->d2d_context->EndDraw();

	constexpr DXGI_PRESENT_PARAMETERS default_present_params {};
	DXGI_PRESENT_PARAMETERS present_params {
		.DirtyRectsCount = static_cast<uint32_t>(renderer->dirty_rects.size()),
		.pDirtyRects = renderer->dirty_rects.data(),
	};
	if (renderer->scrolled) {
		present_params.pScrollRect = &renderer->scrolled_rect;
		present_params.pScrollOffset = &renderer->scroll_offset;
	}

	HRESULT hr = renderer->dxgi_swapchain->Present1(0, 0, renderer->initial_draw ? &default_present_params : &present_params);
	renderer->draw_active = false;
	renderer->initial_draw = false;
	renderer->scrolled = false;
	renderer->dirty_rects.clear();

	if (hr == DXGI_ERROR_DEVICE_REMOVED) {
		HandleDeviceLost(renderer);
	}
}

void RendererRedraw(Renderer *renderer, mpack_node_t params) {
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
			ClearGrid(renderer);
		}
		else if (MPackMatchString(redraw_command_name, "hl_attr_define")) {
			UpdateHighlightAttributes(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_line")) {
            /* mpack_node_print_to_stdout(redraw_command_arr); */
			DrawGridLines(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_cursor_goto")) {
            DrawGridLine(renderer, renderer->cursor.row, 0, renderer->grid_cols);
			UpdateCursorPos(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "mode_info_set")) {
			UpdateCursorModeInfos(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "mode_change")) {
			UpdateCursorMode(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_scroll")) {
			ScrollRegion(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "flush")) {
			DrawCursor(renderer);
			DrawBorderRectangles(renderer);
			FinishDraw(renderer);
		}
	}
}

GridSize RendererPixelsToGridSize(Renderer *renderer, int width, int height) {
	return GridSize {
		.rows = static_cast<int>(height / renderer->line_spacing),
		.cols = static_cast<int>(width / renderer->font_width)
	};
}

GridPoint RendererCursorToGridPoint(Renderer *renderer, int x, int y) {
	return GridPoint {
		.row = static_cast<int>(y / renderer->line_spacing),
		.col = static_cast<int>(x / renderer->font_width)
	};
}
