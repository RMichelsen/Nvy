#include "pch.h"
#include "renderer.h"

#include "common/mpack_helper.h"

#define WIN_CHECK(x) { \
HRESULT ret = x; \
if(ret != S_OK) printf("HRESULT: %s is 0x%X in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}

void RendererInitialize(Renderer *renderer, HWND hwnd, const wchar_t *font, float font_size) {
	WIN_CHECK(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &renderer->d2d_factory));

	renderer->dpi_scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
	renderer->font_size = font_size * renderer->dpi_scale;
	renderer->font = font;

	D2D1_RENDER_TARGET_PROPERTIES target_props = D2D1_RENDER_TARGET_PROPERTIES {
		.type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
		.dpiX = 96.0f,
		.dpiY = 96.0f,
	};

	RECT client_rect;
	GetClientRect(hwnd, &client_rect);
	renderer->pixel_size = D2D1_SIZE_U {
		.width = static_cast<uint32_t>(client_rect.right - client_rect.left),
		.height = static_cast<uint32_t>(client_rect.bottom - client_rect.top)
	};
	D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1_HWND_RENDER_TARGET_PROPERTIES {
		.hwnd = hwnd,
		.pixelSize = renderer->pixel_size,
		.presentOptions = D2D1_PRESENT_OPTIONS_IMMEDIATELY
	};

	WIN_CHECK(renderer->d2d_factory->CreateHwndRenderTarget(target_props, hwnd_props, &renderer->render_target));
	WIN_CHECK(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&renderer->write_factory)));

	RendererUpdateTextFormat(renderer, 0.0f);
}

void RendererUpdateTextFormat(Renderer *renderer, float font_size_delta) {
	renderer->font_size += font_size_delta;

	if (renderer->text_format) {
		renderer->text_format->Release();
	}

	WIN_CHECK(renderer->write_factory->CreateTextFormat(
		renderer->font,
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		renderer->font_size,
		L"en-us",
		&renderer->text_format
	));

	//WIN_CHECK(renderer->text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));
	WIN_CHECK(renderer->text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
	WIN_CHECK(renderer->text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));

	RendererUpdateFontMetrics(renderer);
}

void RendererUpdateFontMetrics(Renderer *renderer) {
	// Create dummy text format to hit test the size of the font
	IDWriteTextLayout *test_text_layout = nullptr;
	WIN_CHECK(renderer->write_factory->CreateTextLayout(
		L"A",
		1,
		renderer->text_format,
		0.0f,
		0.0f,
		&test_text_layout
	));

	DWRITE_HIT_TEST_METRICS metrics;
	float _;
	WIN_CHECK(test_text_layout->HitTestTextPosition(0, 0, &_, &_, &metrics));
	test_text_layout->Release();

	renderer->font_width = metrics.width;
	renderer->font_height = metrics.height;

	renderer->grid_width = static_cast<uint32_t>(renderer->pixel_size.width / renderer->font_width);
	renderer->grid_height = static_cast<uint32_t>(renderer->pixel_size.height / renderer->font_height);
}

void CreateBrush(Renderer *renderer, uint32_t color) {
	ID2D1SolidColorBrush *brush;
	WIN_CHECK(renderer->render_target->CreateSolidColorBrush(
		D2D1::ColorF(color),
		&brush
	));

	if (!renderer->brushes.contains(color)) {
		renderer->brushes.insert({ color, brush });
	}
}

void UpdateDefaultColors(Renderer *renderer, mpack_node_t default_colors) {
	mpack_node_t color_arr = mpack_node_array_at(default_colors, 1);

	// Default colors occupy the first index of the highlight attribs array
	renderer->hl_attribs[0].foreground = static_cast<uint32_t>(mpack_node_array_at(color_arr, 0).data->value.u);
	renderer->hl_attribs[0].background = static_cast<uint32_t>(mpack_node_array_at(color_arr, 1).data->value.u);
	renderer->hl_attribs[0].special = static_cast<uint32_t>(mpack_node_array_at(color_arr, 2).data->value.u);
	renderer->hl_attribs[0].flags = 0;

	CreateBrush(renderer, renderer->hl_attribs[0].foreground);
	CreateBrush(renderer, renderer->hl_attribs[0].background);
	CreateBrush(renderer, renderer->hl_attribs[0].special);
}

void UpdateHighlightAttributes(Renderer *renderer, mpack_node_t highlight_attribs) {
	uint32_t attrib_count = static_cast<uint32_t>(mpack_node_array_length(highlight_attribs));
	for (uint32_t i = 1; i < attrib_count; ++i) {
		uint64_t attrib_index = mpack_node_array_at(mpack_node_array_at(highlight_attribs, i), 0).data->value.u;
		assert(attrib_index <= MAX_HIGHLIGHT_ATTRIBS);

		mpack_node_t attrib_map = mpack_node_array_at(mpack_node_array_at(highlight_attribs, i), 1);

		mpack_node_t foreground = mpack_node_map_cstr_optional(attrib_map, "foreground");
		if (!mpack_node_is_missing(foreground)) {
			renderer->hl_attribs[attrib_index].foreground = static_cast<uint32_t>(foreground.data->value.u);
			CreateBrush(renderer, static_cast<uint32_t>(foreground.data->value.u));
		}
		else {
			renderer->hl_attribs[attrib_index].foreground = DEFAULT_COLOR;
		}

		mpack_node_t background = mpack_node_map_cstr_optional(attrib_map, "background");
		if (!mpack_node_is_missing(background)) {
			renderer->hl_attribs[attrib_index].background = static_cast<uint32_t>(background.data->value.u);
			CreateBrush(renderer, static_cast<uint32_t>(background.data->value.u));
		}
		else {
			renderer->hl_attribs[attrib_index].background = DEFAULT_COLOR;
		}

		mpack_node_t special = mpack_node_map_cstr_optional(attrib_map, "special");
		if (!mpack_node_is_missing(special)) {
			renderer->hl_attribs[attrib_index].special = static_cast<uint32_t>(special.data->value.u);
			CreateBrush(renderer, static_cast<uint32_t>(special.data->value.u));
		}
		else {
			renderer->hl_attribs[attrib_index].special = DEFAULT_COLOR;
		}

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

void DrawTextWithFlags(Renderer *renderer, wchar_t *text, D2D1_RECT_F rect, 
	ID2D1SolidColorBrush *brush, uint16_t flags) {

	uint32_t strlen = static_cast<uint32_t>(lstrlen(text));

	// Allow the drawing to draw as far as it needs to, 
	// this is especially important if using a font with ligatures
	rect.right = renderer->grid_width * renderer->font_width;

	if (flags == 0 || flags == HighlightAttributeFlags::HL_ATTRIB_REVERSE) {
		renderer->render_target->DrawText(
			text,
			strlen,
			renderer->text_format,
			rect,
			brush
		);
	}
	else {
		IDWriteTextLayout *text_layout = nullptr;
		WIN_CHECK(renderer->write_factory->CreateTextLayout(
			text,
			strlen,
			renderer->text_format,
			rect.right - rect.left,
			rect.bottom - rect.top,
			&text_layout
		));
		DWRITE_TEXT_RANGE range {
			.startPosition = 0,
			.length = strlen
		};
		if (flags & HL_ATTRIB_ITALIC) {
			text_layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
		}
		if (flags & HL_ATTRIB_BOLD) {
			text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
		}
		if (flags & HL_ATTRIB_STRIKETHROUGH) {
			text_layout->SetStrikethrough(true, range);
		}
		if (flags & HL_ATTRIB_UNDERLINE) {
			text_layout->SetUnderline(true, range);
		}
		if (flags & HL_ATTRIB_UNDERCURL) {
			text_layout->SetUnderline(true, range);
		}

		renderer->render_target->DrawTextLayout(
			{ rect.left, rect.top },
			text_layout,
			brush
		);

		text_layout->Release();
	}
}

void DrawGridLines(Renderer *renderer, mpack_node_t grid_lines) {
	renderer->render_target->BeginDraw();
	renderer->render_target->SetTransform(D2D1::IdentityMatrix());

	wchar_t wstr_text[8096];

	uint32_t line_count = static_cast<uint32_t>(mpack_node_array_length(grid_lines));
	for (uint32_t i = 1; i < line_count; ++i) {
		mpack_node_t grid_line = mpack_node_array_at(grid_lines, i);

		int64_t grid = mpack_node_array_at(grid_line, 0).data->value.i;
		int64_t row = mpack_node_array_at(grid_line, 1).data->value.i;
		int64_t col_start = mpack_node_array_at(grid_line, 2).data->value.i;

		mpack_node_t cells_array = mpack_node_array_at(grid_line, 3);
		uint32_t cells_array_length = static_cast<uint32_t>(mpack_node_array_length(cells_array));

		HighlightAttribute *hl_id = &renderer->hl_attribs[0];
		for (uint32_t j = 0; j < cells_array_length; ++j) {
			mpack_node_t cells = mpack_node_array_at(cells_array, j);
			uint32_t cells_length = static_cast<uint32_t>(mpack_node_array_length(cells));

			mpack_node_t text = mpack_node_array_at(cells, 0);
			const char *str = mpack_node_str(text);
			uint64_t strlen = mpack_node_strlen(text);

			if (cells_length > 1) {
				hl_id = &renderer->hl_attribs[mpack_node_array_at(cells, 1).data->value.u];
			}

			uint32_t repeat = 1;
			if (cells_length > 2) {
				repeat = static_cast<uint32_t>(mpack_node_array_at(cells, 2).data->value.u);
			}

			for (uint32_t k = 0; k < repeat; ++k) {
				for (int l = 0; l < strlen; ++l) {
					wstr_text[l + k * strlen] = static_cast<wchar_t>(str[l]);
				}
			}
			// Extra byte ensures a null-terminated string
			wstr_text[strlen * repeat] = L'\0';

			// Calculate the rectangle that covers the current cells
			D2D1_RECT_F cells_rect {
				.left = col_start * renderer->font_width,
				.top = row * renderer->font_height,
				.right = col_start * renderer->font_width + renderer->font_width * lstrlen(wstr_text),
				.bottom = (row * renderer->font_height) + renderer->font_height
			};

			ID2D1SolidColorBrush *background_brush = hl_id->background == DEFAULT_COLOR ?
				renderer->brushes.at(renderer->hl_attribs[0].background) :
				renderer->brushes.at(hl_id->background);
			ID2D1SolidColorBrush *foreground_brush = hl_id->foreground == DEFAULT_COLOR ?
				renderer->brushes.at(renderer->hl_attribs[0].foreground) :
				renderer->brushes.at(hl_id->foreground);

			if (hl_id->flags & HL_ATTRIB_REVERSE) {
				ID2D1SolidColorBrush *temp = background_brush;
				background_brush = foreground_brush;
				foreground_brush = temp;
			}

			// Rectangles (background) should be drawn without anti-antialiasing to retain sharpness
			renderer->render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			renderer->render_target->FillRectangle(&cells_rect, background_brush);
			renderer->render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

			DrawTextWithFlags(renderer, wstr_text, cells_rect, foreground_brush, hl_id->flags);
			col_start += strlen * repeat;
		}
	}
	renderer->render_target->EndDraw();
	renderer->render_target->Flush();
}

void RendererRedraw(Renderer *renderer, mpack_node_t params) {
	uint32_t redraw_commands_length = static_cast<uint32_t>(mpack_node_array_length(params));
	for (uint32_t i = 0; i < redraw_commands_length; ++i) {
		mpack_node_t redraw_command_arr = mpack_node_array_at(params, i);
		mpack_node_t redraw_command_name = mpack_node_array_at(redraw_command_arr, 0);

		if (MPackMatchString(redraw_command_name, "default_colors_set")) {
			UpdateDefaultColors(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "hl_attr_define")) {
			UpdateHighlightAttributes(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_line")) {
			DrawGridLines(renderer, redraw_command_arr);
		}
	}
}