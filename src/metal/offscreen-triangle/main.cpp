// Headless Metal-API-on-Vulkan smoke test: indium renders Apple's sample triangle (precompiled AIR metallib)
// into an offscreen texture, reads it back, writes a PNG. Output name carries the Vulkan device name,
// so a software renderer can never be mistaken for hardware.
#include "shader.h"
#include <indium/indium.hpp>
#include <png.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

struct Vertex { float pos[2]; float pad[2]; float color[4]; };  // AAPLVertex: float2 + float4(16-aligned)

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	const char* outdir = argc > 1 ? argv[1] : ".";
	Indium::init(nullptr, 0, !!getenv("INDIUM_TEST_VALIDATION"));
	int rc = 1;
	{
	auto device = Indium::createSystemDefaultDevice();
	if (!device) { fprintf(stderr, "no Vulkan >=1.3 device with timelineSemaphore\n"); return 2; }
	std::string name = device->name();
	bool sw = device->isSoftware();
	printf("vulkan device: %s => %s\n", name.c_str(), sw ? "SOFTWARE (CPU Vulkan device), NOT hardware" : "HW (non-CPU Vulkan device)");
	bool run = true;
	std::thread poll([&] { while (run) device->pollEvents(UINT64_MAX); });

	const size_t W = 512, H = 512;
	std::shared_ptr<Indium::RenderPipelineState> pso;
	std::shared_ptr<Indium::Library> lib;
	const bool tryAir = !!getenv("DAYSETTA_TRY_AIR");  // AIR->SPIR-V (iridium) needs typed-pointer LLVM <=16; see docs/METAL_PLAN.md
	if (tryAir) {
		lib = device->newLibrary(shader, shader_len);
		Indium::RenderPipelineDescriptor pd {};
		pd.vertexFunction = lib->newFunction("vertexShader");
		pd.fragmentFunction = lib->newFunction("fragmentShader");
		pd.colorAttachments.emplace_back();
		pd.colorAttachments[0].pixelFormat = Indium::PixelFormat::RGBA8Unorm;
		pso = device->newRenderPipelineState(pd);
	}
	auto queue = device->newCommandQueue();

	Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, W, H, false);
	td.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	td.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
	auto tex = device->newTexture(td);
	auto buf = device->newBuffer(W * H * 4, Indium::ResourceOptions::StorageModeShared);

	Vertex v[3] = { {{200,-200},{0,0},{1,0,0,1}}, {{-200,-200},{0,0},{0,1,0,1}}, {{0,200},{0,0},{0,0,1,1}} };
	uint32_t vp[2] = { (uint32_t)W, (uint32_t)H };

	auto cb = queue->commandBuffer();
	Indium::RenderPassDescriptor rp {};
	rp.colorAttachments.emplace_back();
	rp.colorAttachments[0].texture = tex;
	rp.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
	rp.colorAttachments[0].storeAction = Indium::StoreAction::Store;
	rp.colorAttachments[0].clearColor = Indium::ClearColor(0.1, 0.1, 0.15, 1);
	auto enc = cb->renderCommandEncoder(rp);
	enc->setViewport(Indium::Viewport { 0, 0, (double)W, (double)H, 0, 1 });
	if (pso) {
		enc->setRenderPipelineState(pso);
		enc->setVertexBytes(v, sizeof(v), 0);
		enc->setVertexBytes(vp, sizeof(vp), 1);
		enc->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);
	}
	enc->endEncoding();
	auto blit = cb->blitCommandEncoder();
	blit->copy(tex, 0, 0, Indium::Origin{0,0,0}, Indium::Size{W,H,1}, buf, 0, W * 4, W * H * 4);
	blit->endEncoding();
	cb->commit();
	cb->waitUntilCompleted();

	auto px = static_cast<uint8_t*>(buf->contents());
	auto at = [&](size_t x, size_t y) { return px + (y * W + x) * 4; };
	auto c = at(W/2, H/2 + 20);  // inside triangle (AIR mode)
	auto bg = at(4, 4);
	// clear colour (0.1,0.1,0.15) -> (26,26,38)
	bool ok = bg[0] >= 25 && bg[0] <= 27 && bg[2] >= 37 && bg[2] <= 39 && (!pso || (c[0] + c[1] + c[2]) > 150);
	printf("center px=%u,%u,%u bg px=%u,%u,%u => %s\n", c[0], c[1], c[2], bg[0], bg[1], bg[2], ok ? "PASS" : "FAIL");

	std::string tag = std::string(pso ? "triangle-AIR-" : "clear-") + (sw ? "lavapipe-SOFTWARE" : "HW");
	std::string path = std::string(outdir) + "/metal-indium-" + tag + "-" + std::string(getenv("DAYSETTA_ARCH") ? getenv("DAYSETTA_ARCH") : "host") + ".png";
	FILE* f = fopen(path.c_str(), "wb");
	auto png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	auto info = png_create_info_struct(png);
	png_init_io(png, f);
	png_set_IHDR(png, info, W, H, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);
	for (size_t y = 0; y < H; y++) png_write_row(png, at(0, y));
	png_write_end(png, info);
	fclose(f);
	printf("wrote %s\n", path.c_str());

	run = false;
	device->wakeupEventLoop();
	poll.join();
	rc = ok ? 0 : 1;
	}  // all Indium objects (incl. cb) die before finit()
	Indium::finit();
	return rc;
}
