#pragma once

constexpr int MAX_HIGHLIGHT_ATTRIBS = 512;
constexpr uint32_t DEFAULT_COLOR = 0x46464646;
enum HighlightAttributeFlags : uint16_t {
	HL_ATTRIB_REVERSE			= 1 << 0,
	HL_ATTRIB_ITALIC			= 1 << 1,
	HL_ATTRIB_BOLD				= 1 << 2,
	HL_ATTRIB_STRIKETHROUGH		= 1 << 3,
	HL_ATTRIB_UNDERLINE			= 1 << 4,
	HL_ATTRIB_UNDERCURL			= 1 << 5
};
struct HighlightAttribute {
	uint32_t foreground;
	uint32_t background;
	uint32_t special;
	uint16_t flags;
};

enum class DrawingEffectType {
	Color,
	Italic,
	Bold,
	Strikethrough,
	Underline,
	Undercurl
};

struct DrawingEffect {
	HighlightAttributeFlags flags;
	DWRITE_TEXT_RANGE text_range;
	ID2D1SolidColorBrush *brush;
};

struct Renderer {
	ID2D1Factory *d2d_factory;
	ID2D1HwndRenderTarget *render_target;
	IDWriteFactory *write_factory;

	IDWriteTextFormat *text_format;

	const wchar_t *font;
	float dpi_scale;
	float font_size;
	float font_height;
	float font_width;

	D2D1_SIZE_U pixel_size;

	uint32_t grid_width;
	uint32_t grid_height;

	std::unordered_map<uint32_t, ID2D1SolidColorBrush *> brushes;
	HighlightAttribute hl_attribs[MAX_HIGHLIGHT_ATTRIBS];
};

void RendererInitialize(Renderer *renderer, HWND hwnd, const wchar_t *font, float font_size);

void RendererUpdateTextFormat(Renderer *renderer, float font_size_delta);
void RendererUpdateFontMetrics(Renderer *renderer);

void RendererRedraw(Renderer *renderer, mpack_node_t params);

void RendererDraw(Renderer *renderer);