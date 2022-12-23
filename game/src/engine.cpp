// NOTE this header must be included before any OpenGL headers
#include "engine/gfx.hpp"
#include "engine.hpp"

// TODO reorder includes
#include "legacy.hpp"
#include "engine/audio.hpp"
#include "sdl.hpp"
#include "string.hpp"

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>

#if _WIN32 || defined(AOE_SDL_NO_PREFIX)
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
#else
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL2/SDL_opengles2.h>
#else
#include <SDL2/SDL_opengl.h>
#endif
#endif

#include "imgui_user.hpp"

#include <cstdio>
#include <cstdint>

#include <mutex>
#include <memory>

#include <fstream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>

#include "debug.hpp"

namespace aoe {

Engine *eng;
std::mutex m_eng;

static const char *connection_modes[] = {"host game", "join game"};

ScenarioSettings::ScenarioSettings()
	: players()
	, fixed_start(true), explored(false), all_technologies(false), cheating(false)
	, square(true), restricted(true), reorder(false), hosting(false), width(48), height(48)
	, popcap(100)
	, age(1), seed(1), villagers(3)
	, res(200, 200, 0, 0) {}

Engine::Engine()
	: net(), show_demo(false), show_debug(false)
	, connection_mode(0), connection_port(32768), connection_host("")
	, menu_state(MenuState::init), next_menu_state(MenuState::init)
	, multiplayer_ready(false), m_show_menubar(true)
	, scn()
	, chat_line(), chat(), server()
	, tp(2), ui_tasks(), ui_mod_id(), popups(), popups_async()
	, tsk_start_server{ invalid_ref }, chat_async(), scn_async(), async_tasks(0)
	, running(false), logic_gamespeed(1.0f), scroll_to_bottom(false), username(), fd(ImGuiFileBrowserFlags_CloseOnEsc), fd2(ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_SelectDirectory), sfx(), music_id(0), music_on(true), game_dir()
	, debug()
	, cfg(*this, "config"), sdl(nullptr), is_fullscreen(false), assets()
	, show_achievements(false), show_timeline(false), show_diplomacy(false)
{
	ZoneScoped;
	std::lock_guard<std::mutex> lk(m_eng);
	if (eng)
		throw std::runtime_error("there can be only one");

	eng = this;
}

Engine::~Engine() {
	ZoneScoped;
	std::lock_guard<std::mutex> lk(m_eng);

	assert(eng);
	eng = nullptr;

	running = false;
	stop_server_now();
}

using namespace aoe::ui;

void Engine::show_music_settings() {
	music_on = !sfx.is_muted_music();

	chkbox("Music enabled", music_on);

	if (music_on)
		sfx.unmute_music();
	else
		sfx.mute_music();
}

void Engine::show_menubar() {
	ZoneScoped;
	if (!m_show_menubar)
		return;

	MenuBar m;

	if (!m)
		return;

	{
		Menu mf;
		if (mf.begin("File")) {
			{
				Menu ms;

				if (ms.begin("Settings"))
					show_music_settings();
			}

			if (mf.item("Quit"))
				throw 0;
		}
	}

	{
		Menu mv;
		if (mv.begin("View")) {
			mv.chkbox("Demo window", show_demo);
			mv.chkbox("Debug stuff", show_debug);

			bool v = is_fullscreen = sdl->window.is_fullscreen();

			mv.chkbox("Fullscreen", is_fullscreen);

			if (v != is_fullscreen)
				sdl->window.set_fullscreen(is_fullscreen);
		}
	}
}

static const std::vector<std::string> music_ids{ "menu", "success", "fail", "game" };

/** Load and validate game assets. */
void Engine::verify_game_data(const std::string &path) {
	// TODO move this in worker thread
	game_dir = path;

	tp.push([this](int id, std::string path) {
		ZoneScoped;
		using namespace io;

		try {
			UI_TaskInfo info(ui_async("Verifying game data", "Locating interface data", id, 4));

			DRS drs_ui(path + "/data/Interfac.drs");

			info.next("Loading interface data");
			drs_ui.open_bkg(DrsId::bkg_main_menu);
			drs_ui.open_bkg(DrsId::bkg_achievements);
			drs_ui.open_bkg(DrsId::bkg_defeat);

			info.next("Load chat audio");

			for (unsigned i = 0; i < (unsigned)TauntId::max; ++i) {
				char buf[8];
				snprintf(buf, sizeof buf, "%03d.wav", i + 1);

				std::string fname(path + "/sound/Taunt" + buf);
				sfx.load_taunt((TauntId)i, fname.c_str());
			}

			DRS drs_sounds(path + "/data/sounds.drs");

			info.next("Load game audio");

			sfx.load_sfx(SfxId::sfx_ui_click, drs_sounds.open_wav(DrsId::sfx_ui_click));
			sfx.load_taunt(TauntId::max, drs_sounds.open_wav(DrsId::sfx_priest_convert2));
		} catch (std::exception &e) {
			fprintf(stderr, "%s: game data verification failed: %s\n", __func__, e.what());
			push_error(std::string("Game data verification failed: ") + e.what());
		}

		// TODO set game dir here
	}, path);
}

void Engine::show_start() {
	ZoneScoped;
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);

	Frame f;

	if (!f.begin("start", ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoCollapse))
		return;

	ImGui::SetWindowSize(vp->WorkSize);

	f.str("Age of Empires");
	f.str("Free and open source remake");

	if (f.btn("Multiplayer")) {
		sfx.play_sfx(SfxId::sfx_ui_click);
		next_menu_state = MenuState::multiplayer_menu;
	}

	if (f.btn("Quit")) {
		sfx.play_sfx(SfxId::sfx_ui_click);
		throw 0;
	}

	ImGui::TextWrapped("%s", "Copyright Age of Empires by Microsoft. Trademark reserved by Microsoft. Remake by Folkert van Verseveld");
}

void Engine::show_init() {
	ZoneScoped;
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);

	Frame f;

	if (!f.begin("init", ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoCollapse))
		return;

	ImGui::SetWindowSize(vp->WorkSize);

	f.str("Age of Empires game setup");
	ImGui::TextWrapped("%s", "In this menu, you can change general settings how the game behaves and where the game assets will be loaded from.");
	ImGui::TextWrapped("%s", "This game is free software. If you have paid for this free software remake, you have been scammed! If you like Age of Empires, please support Microsoft by buying the original game on Steam");

	if (f.btn("Start"))
		next_menu_state = MenuState::start;

	if (f.btn("Quit"))
		throw 0;

	if (f.btn("Set game directory"))
		fd2.Open();

	fd2.Display();

	if (fd2.HasSelected()) {
		std::string path(fd2.GetSelected().string());
		verify_game_data(path);
		fd2.ClearSelected();
	}

	f.combo("music id", music_id, music_ids);

	if (f.btn("Open music"))
		fd.Open();

	fd.Display();

	if (fd.HasSelected()) {
		std::string path(fd.GetSelected().string());
		printf("selected \"%s\"\n", path.c_str());

		MusicId id = (MusicId)music_id;
		sfx.jukebox[id] = path;
		sfx.play_music(id);

		fd.ClearSelected();
	}

	f.sl();

	if (f.btn("Play"))
		sfx.play_music((MusicId)music_id);

	f.sl();

	if (f.btn("Stop"))
		sfx.stop_music();

	show_music_settings();

	ImGui::TextWrapped("%s", "Copyright Age of Empires by Microsoft. Trademark reserved by Microsoft. Remake by Folkert van Verseveld");
}

void Engine::show_multiplayer_menu() {
	ZoneScoped;
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);

	Frame f;

	if (!f.begin("multiplayer menu", ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoCollapse))
		return;

	ImGui::SetWindowSize(vp->WorkSize);

	//f.text("username", username);

	if (ImGui::Combo("connection mode", &connection_mode, connection_modes, IM_ARRAYSIZE(connection_modes)))
		sfx.play_sfx(SfxId::sfx_ui_click);

	if (connection_mode == 1) {
		ImGui::InputText("host", connection_host, sizeof(connection_host));
		ImGui::SameLine();
		if (ImGui::Button("localhost"))
			strncpy0(connection_host, "127.0.0.1", sizeof(connection_host));
	}

	ImGui::InputScalar("port", ImGuiDataType_U16, &connection_port);

	if (f.btn("start")) {
		sfx.play_sfx(SfxId::sfx_ui_click);
		switch (connection_mode) {
			case 0:
				start_server(connection_port);
				break;
			case 1:
				start_client(connection_host, connection_port);
				break;
		}
	}

	ImGui::SameLine();

	if (f.btn("cancel")) {
		sfx.play_sfx(SfxId::sfx_ui_click);
		next_menu_state = MenuState::start;
	}
}

// TODO repurpose or remove
void Engine::display_us() {
	ZoneScoped;

	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	const float vertices[] = {
		-0.5f, -0.5f, 0.0f,
		0.5f, -0.5f, 0.0f,
		0.0f,  0.5f, 0.0f
	};

	gfx::GLbuffer b; // TODO move to eliminate create/delete every frame
	gfx::GLbufferview v(b);

	v.draw(vertices, sizeof(vertices));
	// TODO
}

void Engine::display_ui() {
	ZoneScoped;
	show_menubar();

	switch (menu_state) {
		case MenuState::multiplayer_host:
			show_multiplayer_host();
			break;
		case MenuState::multiplayer_game:
			show_multiplayer_game();
			break;
		case MenuState::multiplayer_menu:
			show_multiplayer_menu();
			break;
		case MenuState::start:
			show_start();
			break;
		default:
			show_init();
			break;
	}

	if (show_demo)
		ImGui::ShowDemoWindow(&show_demo);

	if (show_debug)
		debug.show(show_debug);

	display_ui_tasks();

	if (!popups.empty()) {
		ui::Popup &p = popups.front();
		if (!p.show()) {
			ImGui::CloseCurrentPopup();
			popups.pop();
		}
	}
}

void Engine::display() {
	gfx::glchk();
	display_ui();
	gfx::glchk();
}

void Engine::display_ui_tasks() {
	ZoneScoped;

	// XXX this is racey but okay ish?
	if (!ui_tasks.size())
		return;

	std::lock_guard<std::mutex> lock(m_ui);

	ImGui::OpenPopup("tasks");

	ImGui::SetNextWindowSize(ImVec2(400, 0));

	if (ImGui::BeginPopupModal("tasks", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		int i = 0;
		bool cancellable = false;

		for (auto it = eng->ui_tasks.begin(); it != eng->ui_tasks.end(); ++i) {
			UI_Task &tsk = it->second;

			float f = (float)tsk.steps / tsk.total;
			ImGui::TextWrapped("%s", tsk.title.c_str());
			ImGui::ProgressBar(f, ImVec2(-1, 0));
			if (!tsk.desc.empty())
				ImGui::TextWrapped("%s", tsk.desc.c_str());

			std::string str("Cancel##" + std::to_string(i));

			if ((unsigned)tsk.flags & (unsigned)TaskFlags::cancellable)
				cancellable = true;

			if (((unsigned)tsk.flags & (unsigned)TaskFlags::cancellable) && ImGui::Button(str.c_str()))
				it = eng->ui_tasks.erase(it);
			else
				++it;
		}

		if (cancellable && ImGui::Button("Cancel all tasks")) {
			for (auto it = eng->ui_tasks.begin(); it != eng->ui_tasks.end(); ++i) {
				UI_Task &tsk = it->second;
				if ((unsigned)tsk.flags & (unsigned)TaskFlags::cancellable)
					it = eng->ui_tasks.erase(it);
				else
					++it;
			}
		}

		ImGui::EndPopup();
	}
}

void Engine::push_error(const std::string &msg) {
	std::lock_guard<std::mutex> lock(m_async);
	popups_async.emplace(msg, ui::PopupType::error);
}

void Engine::start_client_now(const char *host, uint16_t port) {
	ZoneScoped;

	client.reset(new Client());
	client->start(host, port);
}

void Engine::start_client(const char *host, uint16_t port) {
	ZoneScoped;

	reserve_threads(1);
	tp.push([this](int id, const char *host, uint16_t port) {
		ZoneScoped;

		try {
			UI_TaskInfo info(ui_async("Starting client", "Creating network area", id, 2));

			client.reset(new Client());

			info.next("Connecting to host");

			client->start(host, port);

			trigger_client_connected();
		} catch (std::exception &e) {
			fprintf(stderr, "%s: cannot connect to server: %s\n", __func__, e.what());
			push_error(std::string("cannot connect to server: ") + e.what());
		}
	}, host, port);
}

void Engine::stop_server_now(IdPoolRef ref) {
	ZoneScoped;

	std::lock_guard<std::mutex> lk(m);
	if (server) {
		server->close();
		tsk_start_server = ref;
	}
}

void Engine::stop_server() {
	ZoneScoped;

	reserve_threads(1);
	tp.push([this](int id) {
		std::lock_guard<std::mutex> lk(m_eng);
		if (eng)
			stop_server_now();
	});
}

void Engine::start_server(uint16_t port) {
	ZoneScoped;

	reserve_threads(2);
	tp.push([this](int id, uint16_t port) {
		ZoneScoped;

		try {
			UI_TaskInfo info(ui_async("Starting server", "Creating network area", id, 2));

			// ensures that tsk_start_server is always in a reliable state
			class TskGuard final {
			public:
				bool good;

				TskGuard(UI_TaskInfo &info) : good(false) {
					std::lock_guard<std::mutex> lock(m_eng);
					if (eng)
						eng->stop_server_now(info.get_ref());
				}

				~TskGuard() {
					std::lock_guard<std::mutex> lock(m_eng);
					if (!eng)
						return;

					eng->tsk_start_server = invalid_ref;

					if (!good)
						eng->stop_server();
					else
						eng->trigger_server_started();
				}
			} guard(info);

			{
				std::lock_guard<std::mutex> lk(m);
				// there should be either no server or an inactive one
				assert(!server || !server->active());
				server.reset(new Server);
			}

			tp.push([this](int id, uint16_t port) {
				server->mainloop(id, port, 1);
			}, port);

			info.next("Connecting to host");

			start_client_now("127.0.0.1", port);

			guard.good = true;
		} catch (std::exception &e) {
			fprintf(stderr, "%s: cannot start server: %s\n", __func__, e.what());
		}
	}, port);
}

void Engine::trigger_async_flags(unsigned f) {
	std::lock_guard<std::mutex> lock(m_async);
	async_tasks |= (unsigned)f;
}

void Engine::trigger_server_started() {
	trigger_async_flags(EngineAsyncTask::server_started);
}

void Engine::trigger_client_connected() {
	trigger_async_flags(EngineAsyncTask::client_connected);
}

void Engine::trigger_multiplayer_stop() {
	trigger_async_flags(EngineAsyncTask::multiplayer_stopped);
}

void Engine::trigger_multiplayer_started() {
	trigger_async_flags(EngineAsyncTask::multiplayer_started);
}

void Engine::trigger_username(const std::string &s) {
	std::lock_guard<std::mutex> lock(m_async);
	username_async = s;
	async_tasks |= (unsigned)EngineAsyncTask::set_username;
}

void Engine::trigger_playermod(const NetPlayerControl &ctl) {
	std::lock_guard<std::mutex> lock(m_async);
	// TODO put in queue
	playermod_async = ctl;
	async_tasks |= (unsigned)EngineAsyncTask::player_mod;
}

bool Engine::is_hosting() {
	std::lock_guard<std::mutex> lk(m);
	return server.get() != nullptr;
}

void Engine::idle() {
	ZoneScoped;
	idle_async();

	if (menu_state != next_menu_state) {
		menu_state = next_menu_state;

		switch (menu_state) {
		case MenuState::start:
			sfx.play_music(MusicId::menu);
			break;
		case MenuState::multiplayer_game:
			sfx.play_music(MusicId::game);
			break;
		}
	}
}

void Engine::idle_async() {
	ZoneScoped;

	std::lock_guard<std::mutex> lock(m_async);

	// copy popups
	for (; !popups_async.empty(); popups_async.pop())
		popups.emplace(popups_async.front());

	if (async_tasks) {
		if (async_tasks & (unsigned)EngineAsyncTask::server_started) {
			scn.hosting = true;
			next_menu_state = MenuState::multiplayer_host;
		}

		if (async_tasks & (unsigned)EngineAsyncTask::client_connected) {
			scn.hosting = false;
			next_menu_state = MenuState::multiplayer_host;
		}

		if (async_tasks & (unsigned)EngineAsyncTask::multiplayer_stopped)
			cancel_multiplayer_host();

		if (async_tasks & (unsigned)EngineAsyncTask::multiplayer_started)
			start_multiplayer_game();

		if (async_tasks & (unsigned)EngineAsyncTask::set_scn_vars)
			set_scn_vars_now(scn_async);

		if (async_tasks & (unsigned)EngineAsyncTask::set_username)
			username = username_async;

		if (async_tasks & (unsigned)EngineAsyncTask::player_mod)
			playermod(playermod_async);
	}

	async_tasks = 0;

	for (; !chat_async.empty(); chat_async.pop())
		chat.emplace_back(chat_async.front());
}

void Engine::playermod(const NetPlayerControl &ctl) {
	switch (ctl.type) {
	case NetPlayerControlType::resize:
		scn.players.resize(ctl.arg);
		break;
	default:
		fprintf(stderr, "%s: ctl type=%u\n", __func__, ctl.type);
		break;
	}
}

void Engine::start_multiplayer_game() {
	ZoneScoped;

	next_menu_state = MenuState::multiplayer_game;
	show_achievements = false;
	show_timeline = false;
	show_diplomacy = false;
}

void Engine::add_chat_text(const std::string &s) {
	std::lock_guard<std::mutex> lk(m_async);
	chat_async.emplace(s);
}

void Engine::set_scn_vars(const ScenarioSettings &scn) {
	std::lock_guard<std::mutex> lk(m_async);
	scn_async = scn;
	async_tasks |= (unsigned)EngineAsyncTask::set_scn_vars;
}

void Engine::set_scn_vars_now(const ScenarioSettings &scn) {
	this->scn.fixed_start = scn.fixed_start;
	this->scn.explored = scn.explored;
	this->scn.all_technologies = scn.all_technologies;
	this->scn.cheating = scn.cheating;
	this->scn.square = scn.square;

	if (!is_hosting())
		this->scn.restricted = scn.restricted;

	this->scn.width = scn.width;
	this->scn.height = scn.height;
	this->scn.popcap = scn.popcap;
	this->scn.age = scn.age;
	this->scn.seed = scn.seed;
	this->scn.villagers = scn.villagers;

	this->scn.res.food = scn.res.food;
	this->scn.res.wood = scn.res.wood;
	this->scn.res.gold = scn.res.gold;
	this->scn.res.stone = scn.res.stone;
}

void Engine::cancel_multiplayer_host() {
	try {
		if (server)
			stop_server();
		else
			client->stop();

		chat.clear();
		next_menu_state = MenuState::start;
	} catch (std::exception &e) {
		fprintf(stderr, "%s: cannot stop multiplayer: %s\n", __func__, e.what());
		push_error(std::string("cannot stop multiplayer: ") + e.what());
	}
}

UI_TaskInfo Engine::ui_async(const std::string &title, const std::string &desc, int thread_id, unsigned steps, TaskFlags flags) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_ui);
	auto ref = ui_tasks.emplace(flags, title, desc, 0, steps);
	return UI_TaskInfo(ref.first->first, (TaskFlags)flags);
}

bool Engine::ui_async_stop(IdPoolRef ref) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_ui);
	return ui_tasks.try_invalidate(ref);
}

UI_TaskInfo::~UI_TaskInfo() {
	ZoneScoped;
	// just tell engine task has completed, we don't care if it succeeds
	std::lock_guard<std::mutex> lock(m_eng);
	if (eng)
		(void)eng->ui_async_stop(*this);
}

/* Throw if task is interruptable. */
static void tsk_check_throw(const UI_TaskInfo &info) {
	if ((unsigned)info.get_flags() & (unsigned)TaskFlags::cancellable)
		throw UI_TaskError("interrupted");
}

void Engine::ui_async_set_desc(UI_TaskInfo &info, const std::string &s) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_ui);
	UI_Task *tsk = ui_tasks.try_get(info.get_ref());
	if (tsk)
		tsk->desc = s;
	else
		tsk_check_throw(info);
}

void Engine::ui_async_set_total(UI_TaskInfo &info, unsigned total) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_ui);
	UI_Task *tsk = ui_tasks.try_get(info.get_ref());
	if (tsk)
		tsk->total = total;
	else
		tsk_check_throw(info);
}

void Engine::ui_async_next(UI_TaskInfo &info) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_ui);
	UI_Task *tsk = ui_tasks.try_get(info.get_ref());
	if (tsk)
		tsk->steps = std::min(tsk->steps + 1u, tsk->total);
	else
		tsk_check_throw(info);
}

void Engine::ui_async_next(UI_TaskInfo &info, const std::string &s) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_ui);
	UI_Task *tsk = ui_tasks.try_get(info.get_ref());
	if (tsk) {
		tsk->steps = std::min(tsk->steps + 1u, tsk->total);
		tsk->desc = s;
	} else {
		tsk_check_throw(info);
	}
}

void UI_TaskInfo::set_total(unsigned total) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_eng);
	if (eng)
		eng->ui_async_set_total(*this, total);
}

void UI_TaskInfo::set_desc(const std::string &s) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_eng);
	if (eng)
		eng->ui_async_set_desc(*this, s);
}

void UI_TaskInfo::next() {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_eng);
	if (eng)
		eng->ui_async_next(*this);
}

void UI_TaskInfo::next(const std::string &s) {
	ZoneScoped;
	std::lock_guard<std::mutex> lock(m_eng);
	if (eng)
		eng->ui_async_next(*this, s);
}

void Engine::tick() {
	ZoneScoped;
}

void Engine::eventloop(int id) {
	ZoneScoped;

	auto last = std::chrono::steady_clock::now();
	double dt = 0;
	bool measured = false;

	while (running.load()) {
		// recompute as logic_gamespeed may change
		double interval_inv = (double)logic_gamespeed * DEFAULT_TICKS_PER_SECOND;
		double interval = 1 / std::max(0.01, interval_inv);

		auto now = std::chrono::steady_clock::now();
		std::chrono::duration<double> elapsed = now - last;
		last = now;
		dt += elapsed.count();

		size_t steps = (size_t)(dt * interval_inv);

		// do steps
		for (; steps; --steps)
			tick();

		dt = fmod(dt, interval);

		unsigned us = 0;

		if (!steps)
			us = (unsigned)(interval * 1000 * 1000);
		// 100000000

		if (us > 500)
			std::this_thread::sleep_for(std::chrono::microseconds(us));
	}
}

void Engine::reserve_threads(int n) {
	if (tp.n_idle() >= n)
		return;

	printf("%s: grow %d\n", __func__, n);
	tp.resize(tp.size() + n);
}

int Engine::mainloop() {
	ZoneScoped;

	running = true;
	// TODO remove/repurpose
#if 0
	reserve_threads(1);
	tp.push([this](int id) { printf("%d\n", !!running.load()); eventloop(id); });
#endif

	SDL sdl;
	this->sdl = &sdl;

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	//ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
	ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForOpenGL(sdl.window, sdl.gl_context);
	ImGui_ImplOpenGL3_Init(sdl.guard.glsl_version);

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont *font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);

	// Our state
	ImVec4 clear_color(0.45f, 0.55f, 0.60f, 1.00f);

	try {
		cfg.load(cfg.path);
	} catch (const std::runtime_error &e) {
		fprintf(stderr, "%s: could not load config: %s\n", __func__, e.what());
	}
#if 0
	gfx::GL gl;
	gfx::GLprogram program;
	{
		gfx::GLshader vs(GL_VERTEX_SHADER);

		vs +=
#include "shaders/shader.vs"
			;

		gfx::GLshader fs(GL_FRAGMENT_SHADER);

		fs +=
#include "shaders/shader.fs"
			;

		vs.build();
		fs.build();

		program += vs;
		program += fs;

		program.build();
	}

	glUseProgram(program.id);
#else
	int ret = 0;

	if ((ret = gl3wInit()) != GL3W_OK) {
		fprintf(stderr, "%s: gl3wInit failed: code=%X\n", __func__, ret);
		return -1;
	}

	GLuint vs;

	gfx::glchk();
	vs = glCreateShader(GL_VERTEX_SHADER);

	const GLchar *src;
	GLint length;

	src =
		#include "shaders/shader.vs"
		;

	length = strlen(src);

	glShaderSource(vs, 1, &src, &length);
	glCompileShader(vs);

	GLint status, log_length;

	glGetShaderiv(vs, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE) {
		glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &log_length);
		std::string buf(log_length + 1, ' ');
		glGetShaderInfoLog(vs, log_length, NULL, buf.data());
		fprintf(stderr, "%s: vertex shader compile error: %s\n", __func__, buf.c_str());
		return -1;
	}

	GLuint fs;

	fs = glCreateShader(GL_FRAGMENT_SHADER);

	src =
		#include "shaders/shader.fs"
		;

	length = strlen(src);

	glShaderSource(fs, 1, &src, &length);
	glCompileShader(fs);

	glGetShaderiv(fs, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE) {
		glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &log_length);
		std::string buf(log_length + 1, ' ');
		glGetShaderInfoLog(fs, log_length, NULL, buf.data());
		fprintf(stderr, "%s: fragment shader compile error: %s\n", __func__, buf.c_str());
		return -1;
	}

	GLuint prog = glCreateProgram();

	glAttachShader(prog, vs);
	glAttachShader(prog, fs);

	glLinkProgram(prog);

	glGetProgramiv(prog, GL_LINK_STATUS, &status);

	if (status != GL_TRUE) {
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_length);
		std::string buf(log_length + 1, ' ');
		glGetProgramInfoLog(prog, log_length, NULL, buf.data());
		fprintf(stderr, "%s: program link error: %s\n", __func__, buf.c_str());
		return -1;
	}

	glDeleteShader(fs);
	glDeleteShader(vs);

	const float vertices[] = {
		-0.5f, -0.5f, 0.0f, // left
		 0.5f, -0.5f, 0.0f, // right
		 0.0f,  0.5f, 0.0f  // top
	};

	GLuint vao, vbo;

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	gfx::glchk();

	ImageCapture ic(WINDOW_WIDTH_MIN, WINDOW_HEIGHT_MIN);
#endif

	// autoload game data if available
	if (!cfg.game_dir.empty())
		verify_game_data(cfg.game_dir);

	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			bool p;

			switch (event.type) {
				case SDL_QUIT:
					done = true;
					break;
				case SDL_WINDOWEVENT:
					ImGui_ImplSDL2_ProcessEvent(&event);
					if (event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(sdl.window))
						done = true;
					break;
				case SDL_KEYUP:
					p = ImGui_ImplSDL2_ProcessEvent(&event);
					if (!(p && io.WantCaptureKeyboard)) {
						switch (event.key.keysym.sym) {
							case SDLK_BACKQUOTE:
								m_show_menubar = !m_show_menubar;
								break;
						}
					}
					break;
				default:
					ImGui_ImplSDL2_ProcessEvent(&event);
					break;
			}
		}

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		//ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		idle();

		gfx::glchk();

		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(prog);
		glBindVertexArray(vao);

		glDrawArrays(GL_TRIANGLES, 0, 3);

		gfx::glchk();

		display();

		// Rendering
		ImGui::Render();
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		ic.step(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		SDL_GL_SwapWindow(sdl.window);
		FrameMark;
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	return 0;
}

}
