#pragma once

/**
 * True Type Font rendering subsystem
 *
 * This is more complicated than one would expect...
 * Since libfreetype does not properly render any TTF type I throw at it,
 * we will use windows native GDI rendering instead to improve font quality.
 */

#include "os_macros.hpp"

#include <memory>
#include <string>

#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_ttf.h>

#include "render.hpp"

namespace genie {

class Font final {
	std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> handle;
public:
	const int ptsize;

	Font(const char* fname, int ptsize);
	Font(TTF_Font* handle, int ptsize = 12);

	SDL_Surface* surf(const char* text, SDL_Color fg);
	SDL_Texture* tex(SimpleRender& r, const char* text, SDL_Color fg);
	SDL_Texture* tex(SimpleRender& r, int& w, int& h, const char* text, SDL_Color fg);

	TTF_Font* data() { return handle.get(); }
};

class Text final {
	Texture m_tex;
public:
	const std::string str;

	Text(SimpleRender& r, Font &f, const std::string& s, SDL_Color fg);

	const Texture& tex() const { return m_tex; }

	void paint(SimpleRender& r, int x, int y);
};

class TextBuf final {
	SimpleRender& r;
	Font& f;
	SDL_Color fg;
	std::unique_ptr<Text> txt;
public:
	TextBuf(SimpleRender& r, Font& f, const std::string& s, SDL_Color fg);

	void append(int ch);
	void erase();
	void clear();
	void str(const std::string& s, SDL_Color fg);
	const std::string& str() const;

	void paint(SimpleRender& r, int x, int y);
};

class TTF final {
public:
	TTF();
	~TTF();
};

}