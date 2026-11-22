---
title: Chasing a black screen
summary: Login completes, every desktop agent runs, and every frame is black. Five rounds of digging later, the pixels turn out to never have been lost.
tags: gpu, debugging
---
For a few days now DaySetta has reached a strange place. WindowServer 27 boots, `loginwindow` logs in, the Dock, the menu bar agent, SystemUIServer and WindowManager all run. WindowServer composites frames and presents them. And every single frame is black.

This is the story of narrowing that down, one layer at a time.

## Round 1: does the GPU even store anything?

The first suspect was our own GPU stack: iokitd plays Apple's paravirtual GPU, reims-vgpu turns the guest's Metal work into Vulkan. A host test drove render-pass stores into a writable surface over guest pages, through the real code: clear, quad, textured and private targets, **4 out of 4 landed**. The GPU path can write pixels.

## Round 2: shared memory, wrong buffers?

Next idea: the guest writes its data somewhere the host never reads. A host test replayed the exact arena remap chain (iokitd's memory, the memfd, WindowServer's mapping, reims) and a write from the client side showed up everywhere it should. The draw bindings were decoded one by one: every draw binds exactly the buffer and offset the guest asked for. Both ruled out.

Along the way a real bug fell out: Darling picked the x86_64 slice of fat binaries when reading code-signing entitlements, so on ARM, WindowServer had no entitlements at all and ColorSync refused it. Fixed, worth keeping, but the screen stayed black.

## Round 3: where exactly do the pixels die?

GPU8 added a switch to reims that reads back every render target after every command buffer, and proved from source that the frame on the display is the very surface the composite draws into. Then it replayed a real boot's composite on the host, through the real deserializer and Vulkan encoder: **black, exactly like the boot.**

The decoded Apple shader explained why. For text, the CoreAnimation uber-shader computes `sampled glyph × per-vertex colour`, and the guest submits that per-vertex colour as **zero** for 22 of the 26 draws. Change only those colours to white in the replay, and the whole composite renders: **105,320 non-zero bytes of real text.** A real boot with the readback switch on confirmed it: all 23 full-screen composites zero, the same zero colours in the vertex buffer.

## Round 4: is it us?

A zero colour that a real Mac would draw correctly would mean we build the wrong shader. So GPU9 checked everything a translator could get wrong: no Metal function constants anywhere in the metallibs, no vertex descriptor to drop, a 48-byte stride that fits every field. Then it followed CoreAnimation's own code: `GlyphDelegate::quad` copies the glyph draw's fill colour verbatim into every vertex. The host is innocent; CoreAnimation really writes zero.

## Round 5: zero how?

The obvious guess was colour management: WindowServer never gets a display colour profile here. GPU10 read the conversion code and killed that idea. With no display colour space the conversion produces **white**, never black, and it never touches alpha. A colour that is zero in all four channels can only come from an **alpha of zero at the moment the draw is created**, inside the app drawing the text, before WindowServer ever sees it.

## Where it stands

Every layer DaySetta owns draws exactly what it is given. The missing piece is upstream: which app hands CoreAnimation a fill colour with zero alpha, and why. Next step is a debugger on the client apps at the exact instruction where the text draw is built. One good hit and the black screen has a name.
