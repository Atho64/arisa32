// Desktop SDL2 shell for the portable core scene runner.
//
// dc4core's dc4scene::SceneRunner renders into a software RGBA frame; this
// header only uploads that frame to a texture. Keeping the shell thin is what
// lets the identical core drive the Android build.
#pragma once
#include "dc4/scene.h"

#include <SDL.h>

namespace dc4scene {

// Presents a dc4core frame buffer to an SDL renderer. Returns the texture
// (owned by the caller) or nullptr.
inline SDL_Texture* frame_to_texture(SDL_Renderer* ren,
                                     const std::vector<uint8_t>& rgba,
                                     int w, int h) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint8_t*>(rgba.data()), w, h, 32, w * 4,
        SDL_PIXELFORMAT_ABGR8888);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

}  // namespace dc4scene
