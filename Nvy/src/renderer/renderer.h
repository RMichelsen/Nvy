#pragma once

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
};

void RendererInitialize(Renderer *renderer, HWND hwnd, const wchar_t *font, float font_size);

void RendererUpdateTextFormat(Renderer *renderer, float font_size_delta);
void RendererUpdateFontMetrics(Renderer *renderer);

void RendererRedraw(Renderer *renderer, mpack_node_t params);

void RendererDraw(Renderer *renderer);