#pragma once

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

struct GridPoint {
	int row;
	int col;
};
struct GridSize {
	int rows;
	int cols;
};

struct CursorModeInfo {
	CursorShape shape;
	float cell_percentage;
	int hl_attrib_index;
};
struct Cursor {
	CursorModeInfo *mode_info;
	int row;
	int col;
};

struct CellProperty {
	uint8_t hl_attrib_id;
	bool is_wide_char;
};

constexpr int MAX_HIGHLIGHT_ATTRIBS = 0xFF;
constexpr int MAX_CURSOR_MODE_INFOS = 64;
constexpr int MAX_FONT_LENGTH = 128;
struct GlyphDrawingEffect;
struct GlyphRenderer;
struct Renderer {
	CursorModeInfo cursor_mode_infos[MAX_CURSOR_MODE_INFOS];
	HighlightAttributes hl_attribs[MAX_HIGHLIGHT_ATTRIBS];
	Cursor cursor;

	GlyphRenderer *glyph_renderer;

	D3D_FEATURE_LEVEL d3d_feature_level;
	ID3D11Device2 *d3d_device;
	ID3D11DeviceContext2 *d3d_context;
	IDXGISwapChain1 *dxgi_swapchain;
	ID2D1Factory5 *d2d_factory;
	ID2D1Device4 *d2d_device;
	ID2D1DeviceContext4 *d2d_context;
	ID2D1Bitmap1 *d2d_target_bitmap;
	ID2D1SolidColorBrush *d2d_background_rect_brush;

	IDWriteFactory4 *dwrite_factory;
	IDWriteTextFormat *dwrite_text_format;

	Vec<RECT> dirty_rects;
	RECT scrolled_rect;
	POINT scroll_offset;
	bool scrolled;

	wchar_t font[MAX_FONT_LENGTH];
	DWRITE_FONT_METRICS1 font_metrics;
	float dpi_scale;
	float font_size;
	float font_height;
	float font_width;
	float font_ascent;
	float line_spacing;

	D2D1_SIZE_U pixel_size;
	int grid_rows;
	int grid_cols;
	wchar_t *grid_chars;
	CellProperty *grid_cell_properties;

	HWND hwnd;
	bool draw_active;
	bool initial_draw;
};

void RendererInitialize(Renderer *renderer, HWND hwnd, const char *font, float font_size);
void RendererShutdown(Renderer *renderer);

void RendererResize(Renderer *renderer, uint32_t width, uint32_t height);
void RendererUpdateFont(Renderer *renderer, float font_size, const char *font_string = "", int strlen = 0);
void RendererRedraw(Renderer *renderer, mpack_node_t params);

GridSize RendererPixelsToGridSize(Renderer *renderer, int width, int height);
GridPoint RendererCursorToGridPoint(Renderer *renderer, int x, int y);
