#include "pch.h"
#include "renderer.h"

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
		.pixelSize = renderer->pixel_size
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
	//WIN_CHECK(renderer->text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

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

void RendererRedraw(Renderer *renderer, mpack_node_t params) {

}

void RendererDraw(Renderer *renderer) {
	renderer->render_target->BeginDraw();
	renderer->render_target->SetTransform(D2D1::IdentityMatrix());
	renderer->render_target->Clear(D2D1::ColorF(D2D1::ColorF::AntiqueWhite));

	ID2D1SolidColorBrush *black_brush = NULL;
	WIN_CHECK(renderer->render_target->CreateSolidColorBrush(
		D2D1::ColorF(D2D1::ColorF::Black),
		&black_brush
	));

	IDWriteTextLayout *test_text_layout = nullptr;
	WIN_CHECK(renderer->write_factory->CreateTextLayout(
		L"Text messaging, or texting, is the act of composing and sending electronic messages, typically consisting of alphabetic and numeric characters, between two or more users of mobile devices, desktops/laptops, or other type of compatible computer.",
		245,
		renderer->text_format,
		2560,
		1440,
		&test_text_layout
	));

	renderer->render_target->DrawTextLayout(
		{ 0.0f, 0.0f },
		test_text_layout,
		black_brush
	);

	test_text_layout->Release();
	black_brush->Release();

	renderer->render_target->EndDraw(nullptr, nullptr);
}