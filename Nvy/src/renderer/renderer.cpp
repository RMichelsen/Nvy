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
		.pixelFormat = D2D1::PixelFormat(),
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

	WIN_CHECK(renderer->text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
	WIN_CHECK(renderer->text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

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

CursorPos RendererTranslateMousePosToGrid(Renderer *renderer, POINTS mouse_pos) {
	return CursorPos {
		.row = static_cast<int>(mouse_pos.y / renderer->font_height),
		.col = static_cast<int>(mouse_pos.x / renderer->font_width)
	};
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

void ApplyHighlightAttributes(Renderer *renderer, HighlightAttributes *hl_attribs,
	IDWriteTextLayout *text_layout, uint32_t start, uint32_t end) {
	ID2D1SolidColorBrush *brush = [=]() {
		if (hl_attribs->flags & HL_ATTRIB_REVERSE) {
			return hl_attribs->background == DEFAULT_COLOR ?
				renderer->brushes.at(renderer->hl_attribs[0].background) :
				renderer->brushes.at(hl_attribs->background);
		}
		else {
			return hl_attribs->foreground == DEFAULT_COLOR ?
				renderer->brushes.at(renderer->hl_attribs[0].foreground) :
				renderer->brushes.at(hl_attribs->foreground);
		}
	}();

	DWRITE_TEXT_RANGE range {
		.startPosition = start,
		.length = (end - start)
	};
	text_layout->SetDrawingEffect(brush, range);
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
	}
}

void DrawBackgroundRect(Renderer *renderer, D2D1_RECT_F rect, HighlightAttributes *hl_attribs) {
	ID2D1SolidColorBrush *brush = [=]() {
		if (hl_attribs->flags & HL_ATTRIB_REVERSE) {
			return hl_attribs->foreground == DEFAULT_COLOR ?
				renderer->brushes.at(renderer->hl_attribs[0].foreground) :
				renderer->brushes.at(hl_attribs->foreground);
		}
		else {
			return hl_attribs->background == DEFAULT_COLOR ?
				renderer->brushes.at(renderer->hl_attribs[0].background) :
				renderer->brushes.at(hl_attribs->background);
		}
	}();

	renderer->render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	renderer->render_target->FillRectangle(&rect, brush);
	renderer->render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void DrawForegroundText(Renderer *renderer, wchar_t *text, uint32_t text_length,
	D2D1_RECT_F rect, HighlightAttributes *hl_attribs) {
	IDWriteTextLayout *text_layout = nullptr;
	WIN_CHECK(renderer->write_factory->CreateTextLayout(
		text,
		text_length,
		renderer->text_format,
		rect.right - rect.left,
		rect.bottom - rect.top,
		&text_layout
	));
	ApplyHighlightAttributes(renderer, hl_attribs, text_layout, 0, text_length);

	ID2D1SolidColorBrush *default_brush = renderer->brushes.at(renderer->hl_attribs[0].foreground);
	renderer->render_target->DrawTextLayout(
		{ rect.left, rect.top },
		text_layout,
		default_brush,
		D2D1_DRAW_TEXT_OPTIONS_CLIP
	);

	text_layout->Release();
}

void DrawGridLine(Renderer *renderer, uint32_t row) {
	uint32_t base = row * renderer->grid_width;
	D2D1_RECT_F rect {
		.left = 0 * renderer->font_width,
		.top = row * renderer->font_height,
		.right = renderer->grid_width * renderer->font_width,
		.bottom = (row * renderer->font_height) + renderer->font_height
	};

	IDWriteTextLayout *text_layout = nullptr;
	WIN_CHECK(renderer->write_factory->CreateTextLayout(
		&renderer->grid_chars[base],
		renderer->grid_width,
		renderer->text_format,
		rect.right - rect.left,
		rect.bottom - rect.top,
		&text_layout
	));

	uint8_t hl_attrib_id = renderer->grid_hl_attrib_ids[base];
	uint32_t col_offset = 0;
	for (uint32_t i = 0; i < renderer->grid_width; ++i) {
		if (renderer->grid_hl_attrib_ids[base + i] != hl_attrib_id) {
			D2D1_RECT_F rect {
				.left = col_offset * renderer->font_width,
				.top = row * renderer->font_height,
				.right = col_offset * renderer->font_width + renderer->font_width * (i - col_offset),
				.bottom = (row * renderer->font_height) + renderer->font_height
			};
			DrawBackgroundRect(renderer, rect, &renderer->hl_attribs[hl_attrib_id]);
			ApplyHighlightAttributes(renderer, &renderer->hl_attribs[hl_attrib_id], text_layout, col_offset, i);

			hl_attrib_id = renderer->grid_hl_attrib_ids[base + i];
			col_offset = i;
		}
	}
	if (col_offset != renderer->grid_width - 1) {
		D2D1_RECT_F rect {
			.left = col_offset * renderer->font_width,
			.top = row * renderer->font_height,
			.right = renderer->font_width * renderer->grid_width,
			.bottom = (row * renderer->font_height) + renderer->font_height
		};
		DrawBackgroundRect(renderer, rect, &renderer->hl_attribs[hl_attrib_id]);
		ApplyHighlightAttributes(renderer, &renderer->hl_attribs[hl_attrib_id], text_layout, col_offset, renderer->grid_width);
	}

	ID2D1SolidColorBrush *default_brush = renderer->brushes.at(renderer->hl_attribs[0].foreground);
	renderer->render_target->DrawTextLayout(
		{ rect.left, rect.top },
		text_layout,
		default_brush,
		D2D1_DRAW_TEXT_OPTIONS_CLIP
	);

	text_layout->Release();
}

void DrawGridLines(Renderer *renderer, mpack_node_t grid_lines) {
	assert(renderer->grid_chars != nullptr);
	assert(renderer->grid_hl_attrib_ids != nullptr);

	uint32_t line_count = static_cast<uint32_t>(mpack_node_array_length(grid_lines));
	for (uint32_t i = 1; i < line_count; ++i) {
		mpack_node_t grid_line = mpack_node_array_at(grid_lines, i);

		uint32_t row = static_cast<uint32_t>(mpack_node_array_at(grid_line, 1).data->value.i);
		uint32_t col_start = static_cast<uint32_t>(mpack_node_array_at(grid_line, 2).data->value.i);

		mpack_node_t cells_array = mpack_node_array_at(grid_line, 3);
		uint32_t cells_array_length = static_cast<uint32_t>(mpack_node_array_length(cells_array));

		uint32_t hl_attrib_id = 0;
		for (uint32_t j = 0; j < cells_array_length; ++j) {
			mpack_node_t cells = mpack_node_array_at(cells_array, j);
			uint32_t cells_length = static_cast<uint32_t>(mpack_node_array_length(cells));

			mpack_node_t text = mpack_node_array_at(cells, 0);
			const char *str = mpack_node_str(text);
			uint32_t strlen = static_cast<uint32_t>(mpack_node_strlen(text));

			assert(strncmp(str, "", strlen));

			if (cells_length > 1) {
				hl_attrib_id = static_cast<uint32_t>(mpack_node_array_at(cells, 1).data->value.u);
			}

			uint32_t repeat = 1;
			if (cells_length > 2) {
				repeat = static_cast<uint32_t>(mpack_node_array_at(cells, 2).data->value.u);
			}

			uint32_t wstrlen = MultiByteToWideChar(CP_UTF8, 0, str, strlen, nullptr, 0);
			uint32_t wstrlen_with_repetitions = static_cast<uint64_t>(wstrlen) * repeat;

			uint32_t offset = row * renderer->grid_width + col_start;
			memset(&renderer->grid_hl_attrib_ids[offset], hl_attrib_id, wstrlen_with_repetitions);
			for (uint32_t k = 0; k < repeat; ++k) {
				uint32_t idx = offset + (k * wstrlen);
				MultiByteToWideChar(CP_UTF8, 0, str, strlen, &renderer->grid_chars[idx], (renderer->grid_width * renderer->grid_height) - idx);
			}

			col_start += wstrlen_with_repetitions;
		}
	}

	for (uint32_t i = 1; i < line_count; ++i) {
		mpack_node_t grid_line = mpack_node_array_at(grid_lines, i);
		uint32_t row = static_cast<uint32_t>(mpack_node_array_at(grid_line, 1).data->value.i);
		DrawGridLine(renderer, row);
	}
}

void UpdateGridSize(Renderer *renderer, mpack_node_t grid_resize) {
	mpack_node_t grid_resize_params = mpack_node_array_at(grid_resize, 1);
	uint64_t grid_width = mpack_node_array_at(grid_resize_params, 1).data->value.u;
	uint64_t grid_height = mpack_node_array_at(grid_resize_params, 2).data->value.u;

	if (renderer->grid_chars == nullptr ||
		renderer->grid_hl_attrib_ids == nullptr ||
		renderer->grid_width != grid_width ||
		renderer->grid_height != grid_height) {
		renderer->grid_chars = reinterpret_cast<wchar_t *>(calloc(grid_width * grid_height, sizeof(wchar_t)));
		renderer->grid_hl_attrib_ids = reinterpret_cast<uint8_t *>(calloc(grid_width * grid_height, sizeof(uint8_t)));
	}
}

D2D1_RECT_F GetCursorRect(Renderer *renderer) {
	D2D1_RECT_F rect = renderer->cursor.cell_rect;
	if (renderer->cursor.mode_info) {
		switch (renderer->cursor.mode_info->shape) {
		case CursorShape::None: {
		} return rect;
		case CursorShape::Block: {
		} return rect;
		case CursorShape::Vertical: {
			rect.right -= (1 - renderer->cursor.mode_info->cell_percentage * 0.5f) * renderer->font_width;
		} return rect;
		case CursorShape::Horizontal: {
			rect.top += (1 - renderer->cursor.mode_info->cell_percentage * 0.5f) * renderer->font_height;
		} return rect;
		}
	}
	return rect;
}

void UpdateCursorPos(Renderer *renderer, mpack_node_t cursor_goto) {
	// Redraw line before moving cursor
	DrawGridLine(renderer, renderer->cursor.row);

	mpack_node_t cursor_goto_params = mpack_node_array_at(cursor_goto, 1);
	renderer->cursor.row = static_cast<uint32_t>(mpack_node_array_at(cursor_goto_params, 1).data->value.u);
	renderer->cursor.col = static_cast<uint32_t>(mpack_node_array_at(cursor_goto_params, 2).data->value.u);
	renderer->cursor.grid_offset = renderer->cursor.row * renderer->grid_width + renderer->cursor.col;
	renderer->cursor.cell_rect = D2D1_RECT_F {
		.left = renderer->cursor.col * renderer->font_width,
		.top = renderer->cursor.row * renderer->font_height,
		.right = renderer->cursor.col * renderer->font_width + renderer->font_width,
		.bottom = renderer->cursor.row * renderer->font_height + renderer->font_height
	};
}

void UpdateCursorMode(Renderer *renderer, mpack_node_t mode_change) {
	mpack_node_t mode_change_params = mpack_node_array_at(mode_change, 1);
	renderer->cursor.mode_info = &renderer->cursor_mode_infos[mpack_node_array_at(mode_change_params, 1).data->value.u];
}

void UpdateCursorModeInfos(Renderer *renderer, mpack_node_t mode_info_set_params) {
	mpack_node_t mode_info_params = mpack_node_array_at(mode_info_set_params, 1);
	mpack_node_t mode_infos = mpack_node_array_at(mode_info_params, 1);
	uint32_t mode_infos_length = static_cast<uint32_t>(mpack_node_array_length(mode_infos));
	assert(mode_infos_length <= MAX_CURSOR_MODE_INFOS);

	for (uint32_t i = 0; i < mode_infos_length; ++i) {
		mpack_node_t mode_info_map = mpack_node_array_at(mode_infos, i);

		renderer->cursor_mode_infos[i].shape = CursorShape::None;
		mpack_node_t cursor_shape = mpack_node_map_cstr_optional(mode_info_map, "cursor_shape");
		if (!mpack_node_is_missing(cursor_shape)) {
			const char *cursor_shape_str = mpack_node_str(cursor_shape);
			uint64_t strlen = mpack_node_strlen(cursor_shape);
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
			renderer->cursor_mode_infos[i].cell_percentage = static_cast<float>(cell_percentage.data->value.u) / 100.0f;
		}

		renderer->cursor_mode_infos[i].hl_attrib_index = 0;
		mpack_node_t hl_ttrib_index = mpack_node_map_cstr_optional(mode_info_map, "attr_id");
		if (!mpack_node_is_missing(hl_ttrib_index)) {
			renderer->cursor_mode_infos[i].hl_attrib_index = static_cast<uint32_t>(hl_ttrib_index.data->value.u);
		}
	}
}

void ScrollRegion(Renderer *renderer, mpack_node_t scroll_region) {
	mpack_node_t scroll_region_params = mpack_node_array_at(scroll_region, 1);

	int top = static_cast<int>(mpack_node_array_at(scroll_region_params, 1).data->value.u);
	int bot = static_cast<int>(mpack_node_array_at(scroll_region_params, 2).data->value.u);
	int left = static_cast<int>(mpack_node_array_at(scroll_region_params, 3).data->value.u);
	int right = static_cast<int>(mpack_node_array_at(scroll_region_params, 4).data->value.u);
	int rows = static_cast<int>(mpack_node_array_at(scroll_region_params, 5).data->value.u);
	int cols = static_cast<int>(mpack_node_array_at(scroll_region_params, 6).data->value.u);

	// Currently nvim does not do any horizontal scrolling
	assert(cols == 0);

	int buffer_bot_row = static_cast<int>(renderer->grid_height - 3);

	// This part is slightly cryptic, basically we're just
	// iterating from top to bottom or vice versa depending on scroll direction.
	// When scrolling from the top we can skip the first line, since it will be overwritten,
	// similarly we can skip the last line when scrolling from the bottom. Ranges are end-exclusive,
	// hence the -2.
	int start_row = rows > 0 ? top + 1 : bot - 2;
	int increment = rows > 0 ? 1 : -1;
	for (int i = start_row; rows > 0 ? i < bot : i >= top; i += increment) {
		int target_row = i - rows;
		if (target_row < 0 || target_row > buffer_bot_row) {
			continue;
		}

		memcpy(
			&renderer->grid_chars[target_row * renderer->grid_width + left],
			&renderer->grid_chars[i * renderer->grid_width + left],
			(static_cast<uint64_t>(right) - left) * sizeof(wchar_t)
		);

		memcpy(
			&renderer->grid_hl_attrib_ids[target_row * renderer->grid_width + left],
			&renderer->grid_hl_attrib_ids[i * renderer->grid_width + left],
			static_cast<uint64_t>(right) - left
		);
	}
}

void DrawCursor(Renderer *renderer) {
	HighlightAttributes hl_attribs = renderer->hl_attribs[renderer->cursor.mode_info->hl_attrib_index];
	wchar_t cursor_char = renderer->grid_chars[renderer->cursor.grid_offset];

	DrawBackgroundRect(renderer, renderer->cursor.cell_rect, &renderer->hl_attribs[renderer->grid_hl_attrib_ids[renderer->cursor.grid_offset]]);
	DrawForegroundText(renderer, &cursor_char, 1, renderer->cursor.cell_rect, &renderer->hl_attribs[renderer->grid_hl_attrib_ids[renderer->cursor.grid_offset]]);

	if (renderer->cursor.mode_info->hl_attrib_index == 0) {
		hl_attribs.flags ^= HL_ATTRIB_REVERSE;
	}
	
	D2D1_RECT_F cursor_rect = GetCursorRect(renderer);
	DrawBackgroundRect(renderer, cursor_rect, &hl_attribs);
	DrawForegroundText(renderer, &cursor_char, 1, cursor_rect, &hl_attribs);
}

void RendererRedraw(Renderer *renderer, mpack_node_t params) {
	renderer->render_target->BeginDraw();
	renderer->render_target->SetTransform(D2D1::IdentityMatrix());

	uint32_t redraw_commands_length = static_cast<uint32_t>(mpack_node_array_length(params));

	for (uint32_t i = 0; i < redraw_commands_length; ++i) {
		mpack_node_t redraw_command_arr = mpack_node_array_at(params, i);
		mpack_node_t redraw_command_name = mpack_node_array_at(redraw_command_arr, 0);

		if (MPackMatchString(redraw_command_name, "grid_resize")) {
			UpdateGridSize(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "default_colors_set")) {
			UpdateDefaultColors(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "default_colors_set")) {
			UpdateDefaultColors(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "hl_attr_define")) {
			UpdateHighlightAttributes(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_line")) {
			DrawGridLines(renderer, redraw_command_arr);
		}
		else if (MPackMatchString(redraw_command_name, "grid_cursor_goto")) {
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
			for (uint32_t i = 0; i < renderer->grid_height; ++i) {
				DrawGridLine(renderer, i);
			}
		}
	}

	DrawCursor(renderer);

	renderer->render_target->EndDraw();
	renderer->render_target->Flush();
}