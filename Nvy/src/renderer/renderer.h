#pragma once

constexpr int MAX_HIGHLIGHT_ATTRIBS = 0xFF;
constexpr int MAX_CURSOR_MODE_INFOS = 64;
constexpr uint32_t DEFAULT_COLOR = 0x46464646;
enum HighlightAttributeFlags : uint16_t {
	HL_ATTRIB_REVERSE			= 1 << 0,
	HL_ATTRIB_ITALIC			= 1 << 1,
	HL_ATTRIB_BOLD				= 1 << 2,
	HL_ATTRIB_STRIKETHROUGH		= 1 << 3,
	HL_ATTRIB_UNDERLINE			= 1 << 4,
	HL_ATTRIB_UNDERCURL			= 1 << 5
};
struct HighlightAttributes {
	uint32_t foreground;
	uint32_t background;
	uint32_t special;
	uint16_t flags;
};

enum class CursorShape {
	None,
	Block,
	Vertical,
	Horizontal
};
struct CursorModeInfo {
	CursorShape shape;
	float cell_percentage;
	uint32_t hl_attrib_index;
};
struct Cursor {
	CursorModeInfo *mode_info;
	D2D1_RECT_F cell_rect;
	uint32_t row;
	uint32_t col;
	uint32_t grid_offset;
};

struct Renderer {
	std::unordered_map<uint32_t, ID2D1SolidColorBrush *> brushes;
	CursorModeInfo cursor_mode_infos[MAX_CURSOR_MODE_INFOS];
	HighlightAttributes hl_attribs[MAX_HIGHLIGHT_ATTRIBS];
	Cursor cursor;

	ID2D1Factory *d2d_factory;
	ID2D1HwndRenderTarget *render_target;
	IDWriteFactory *write_factory;
	IDWriteTextFormat *text_format;

	float dpi_scale;
	const wchar_t *font;
	float font_size;
	float font_height;
	float font_width;

	D2D1_SIZE_U pixel_size;

	uint32_t grid_width;
	uint32_t grid_height;
	wchar_t *grid_chars;
	uint8_t *grid_hl_attrib_ids;
};

void RendererInitialize(Renderer *renderer, HWND hwnd, const wchar_t *font, float font_size);

void RendererUpdateTextFormat(Renderer *renderer, float font_size_delta);
void RendererUpdateFontMetrics(Renderer *renderer);

void RendererRedraw(Renderer *renderer, mpack_node_t params);
