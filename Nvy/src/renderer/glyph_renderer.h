#pragma once

struct DECLSPEC_UUID("8d4d2884-e4d9-11ea-87d0-0242ac130003") ColorDrawingEffect : public IUnknown {
	ColorDrawingEffect(uint32_t color) : ref_count(0), color(color) {}

	inline ULONG AddRef() override {
		return InterlockedIncrement(&ref_count);

	}
	inline ULONG Release() override {
		ULONG new_count = InterlockedDecrement(&ref_count);
		if (new_count == 0) {
			delete this;
			return 0;
		}
		return new_count;
	}

	HRESULT QueryInterface(REFIID riid, void **ppv_object) override;

	ULONG ref_count;
	uint32_t color;
};

struct Renderer;
struct GlyphRenderer : public IDWriteTextRenderer {
	GlyphRenderer();

	HRESULT DrawGlyphRun(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y,
		DWRITE_MEASURING_MODE measuring_mode, DWRITE_GLYPH_RUN const *glyph_run, 
		DWRITE_GLYPH_RUN_DESCRIPTION const *glyph_run_description, IUnknown *client_drawing_effect) override;

	HRESULT DrawInlineObject(void *client_drawing_context, float origin_x, float origin_y, IDWriteInlineObject *inline_obj,
		BOOL is_sideways, BOOL is_right_to_left, IUnknown *client_drawing_effect) override;

	HRESULT DrawStrikethrough(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y,
		DWRITE_STRIKETHROUGH const *strikethrough, IUnknown *client_drawing_effect) override;

	HRESULT DrawUnderline(void *client_drawing_context, float baseline_origin_x, float baseline_origin_y, 
		DWRITE_UNDERLINE const *underline, IUnknown *client_drawing_effect) override;

	HRESULT IsPixelSnappingDisabled(void *client_drawing_context, BOOL *is_disabled) override;
	HRESULT GetCurrentTransform(void *client_drawing_context, DWRITE_MATRIX *transform) override;
	HRESULT GetPixelsPerDip(void *client_drawing_context, float *pixels_per_dip) override;

	ULONG AddRef() override;
	ULONG Release() override;
	HRESULT QueryInterface(REFIID riid, void **ppv_object) override;

	ULONG ref_count;
};