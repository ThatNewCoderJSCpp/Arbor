#include "arbor/examples.hpp"
#include "arbor/expr.hpp"
#include "arbor/graph.hpp"
#include "arbor/kinds.hpp"
#include "arbor/library.hpp"
#include "arbor/png.hpp"
#include "arbor/program.hpp"
#include "arbor/render.hpp"
#include "arbor/scene.hpp"

#include <cctype>
#include <grove/canvas.hpp>
#include <grove/dom_tools.hpp>
#include <grove/grove.hpp>
#include <grove/runtime/runtime.hpp>

#if defined(__ANDROID__)
#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

bool arbor_ask_to_end(const std::string& title, const std::string& message);


#ifndef ARBOR_VERSION_STRING
#define ARBOR_VERSION_STRING "0.1.0"
#endif

namespace fs = std::filesystem;
using namespace arbor;

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string clean(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

std::string arg(const std::vector<std::string>& args, std::size_t i) {
    return i < args.size() ? args[i] : std::string();
}

std::vector<std::string> fields_of(const std::vector<std::string>& args) {
    if (args.size() == 1) return split(args[0], '\t');
    return args;
}

std::optional<std::string> read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_text(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    return !ec;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

fs::path data_dir() {
    const std::string over = env("ARBOR_DATA_DIR");
    if (!over.empty()) return over;
#if defined(_WIN32)
    const std::string appdata = env("APPDATA");
    if (!appdata.empty()) return fs::path(appdata) / "Arbor";
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    if (!home.empty()) return fs::path(home) / "Library" / "Application Support" / "Arbor";
#else
    const std::string xdg = env("XDG_DATA_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "arbor";
    const std::string home = env("HOME");
    if (!home.empty()) return fs::path(home) / ".local" / "share" / "arbor";
#endif
    return fs::current_path() / "arbor-data";
}

[[maybe_unused]] std::string find_ui() {
    std::vector<fs::path> candidates;
    const std::string over = env("ARBOR_UI_DIR");
    if (!over.empty()) candidates.push_back(over);
    const fs::path exe = fs::path(grove::executable_path()).parent_path();
    candidates.push_back(exe / "ui");
    candidates.push_back(exe.parent_path() / "share" / "arbor" / "ui");
#ifdef ARBOR_SOURCE_UI_DIR
    candidates.push_back(ARBOR_SOURCE_UI_DIR);
#endif
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c / "arbor.grove", ec)) return (c / "arbor.grove").string();
    }
    return std::string();
}

std::string trim_number(double v, int digits = 6) {
    std::string s = format_double(v, digits);
    return s;
}

struct Settings {
    std::string theme = "dark";
    std::string bg = "#1e1e2e";
    std::string panel = "#181825";
    std::string text = "#cdd6f4";
    std::string accent = "#89b4fa";
    std::string number_type = "short-float";
    bool precise_plot = false;
    std::string box_3d = "5";
    std::string zbox_3d = "";
    std::string bound_2d_xmin;
    std::string bound_2d_xmax;
    std::string bound_2d_ymin;
    std::string bound_2d_ymax;
    std::string on_close = "ask";
    int quality = 2;
    bool smooth_3d = true;
    bool readout_symbols = true;
    bool readout_values = true;
    double ui_scale = 1.0;

    std::map<std::string, std::string> to_map() const {
        return {
            { "theme", theme }, { "bg", bg }, { "panel", panel }, { "text", text }, { "accent", accent },
            { "number-type", number_type }, { "precise-plot", precise_plot ? "1" : "0" }, { "on-close", on_close },
            { "quality", std::to_string(quality) }, { "smooth-3d", smooth_3d ? "1" : "0" }, { "readout-symbols", readout_symbols ? "1" : "0" }, { "readout-values", readout_values ? "1" : "0" }, { "ui-scale", trim_number(ui_scale, 4) },
            { "box-3d", box_3d }, { "zbox-3d", zbox_3d }, { "bound-2d-x-min", bound_2d_xmin }, { "bound-2d-x-max", bound_2d_xmax },
            { "bound-2d-y-min", bound_2d_ymin }, { "bound-2d-y-max", bound_2d_ymax },
        };
    }

    bool set(const std::string& key, const std::string& value) {
        if (key == "theme") theme = value == "light" || value == "custom" ? value : "dark";
        else if (key == "bg") bg = value;
        else if (key == "panel") panel = value;
        else if (key == "text") text = value;
        else if (key == "accent") accent = value;
        else if (key == "number-type") {
            NumberType t = value == "double" || value == "hardware" ? NumberType::ShortFloat : number_type_from_id(value);
            if (t == NumberType::Hardware) t = NumberType::ShortFloat;
            number_type = number_type_id(t);
        }
        else if (key == "box-3d") box_3d = value;
        else if (key == "zbox-3d") zbox_3d = value;
        else if (key == "bound-2d-x-min") bound_2d_xmin = value;
        else if (key == "bound-2d-x-max") bound_2d_xmax = value;
        else if (key == "bound-2d-y-min") bound_2d_ymin = value;
        else if (key == "bound-2d-y-max") bound_2d_ymax = value;
        else if (key == "precise-plot") precise_plot = value == "1" || value == "true";
        else if (key == "on-close") on_close = value == "save" || value == "discard" ? value : "ask";
        else if (key == "quality") quality = std::clamp(std::atoi(value.c_str()), 1, 3);
        else if (key == "smooth-3d") smooth_3d = value == "1" || value == "true";
        else if (key == "readout-symbols") readout_symbols = value == "1" || value == "true";
        else if (key == "readout-values") readout_values = value == "1" || value == "true";
        else if (key == "ui-scale") ui_scale = std::clamp(std::atof(value.c_str()), 0.75, 2.5);
        else return false;
        return true;
    }

    std::string save() const {
        std::string out = "settings {\n";
        for (const auto& [k, v] : to_map()) out += "    " + k + ": \"" + v + "\";\n";
        return out + "}\n";
    }

    void load(const std::string& text) {
        for (std::string line : split(text, '\n')) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            auto strip = [](std::string s) {
                const std::size_t a = s.find_first_not_of(" \t\r");
                const std::size_t b = s.find_last_not_of(" \t\r;");
                s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                return s;
            };
            set(strip(key), strip(value));
        }
    }

    Theme canvas_theme() const {
        Theme t;
        if (theme == "light") {
            t.background = rgba(250, 250, 252);
            t.grid_minor = rgba(0, 0, 0, 14);
            t.grid_major = rgba(0, 0, 0, 34);
            t.axis = rgba(40, 40, 50, 190);
            t.text = rgba(40, 40, 50, 230);
            t.box = rgba(40, 40, 50, 80);
            t.accent = rgba(37, 99, 235);
            t.dark = false;
        } else if (theme == "custom") {
            Rgba b, x, a;
            if (!parse_color(bg, b)) b = rgba(30, 30, 46);
            if (!parse_color(text, x)) x = rgba(205, 214, 244);
            if (!parse_color(accent, a)) a = rgba(137, 180, 250);
            b.a = 255;
            t.background = b;
            t.dark = b.r * 0.3 + b.g * 0.59 + b.b * 0.11 < 128;
            t.grid_minor = with_alpha(x, t.dark ? 0.07 : 0.06);
            t.grid_major = with_alpha(x, t.dark ? 0.16 : 0.14);
            t.axis = with_alpha(x, 0.7);
            t.text = with_alpha(x, 0.88);
            t.box = with_alpha(x, 0.35);
            t.accent = a;
        }
        return t;
    }
};

class Builder {
public:
    using Done = std::function<void()>;

    Builder() : m_thread([this] { loop(); }) {}

    ~Builder() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
            m_cancel = true;
        }
        m_cv.notify_all();
        for (int i = 0; i < 300 && m_busy.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (m_busy.load()) {
            std::cerr << "arbor: a drawing didn't stop, so Arbor is closing without waiting for it\n";
            std::_Exit(0);
        }
        if (m_thread.joinable()) m_thread.join();
    }

    void cancel() { m_cancel = true; }
    bool cancelling() const { return m_cancel.load() && m_busy.load(); }

    double busy_seconds() const {
        const long long started = m_started_ms.load();
        if (started == 0 || !m_busy.load()) return 0.0;
        return static_cast<double>(now_ms() - started) / 1000.0;
    }

    static long long now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void submit(BuildRequest req, bool precise) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending = std::move(req);
            m_precise = precise;
            m_cancel = true;
        }
        m_cv.notify_all();
    }

    std::shared_ptr<Scene> take() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::shared_ptr<Scene> s = std::move(m_done);
        m_done.reset();
        return s;
    }

    bool refining() const { return m_refining.load(); }
    bool busy() const { return m_busy.load(); }

private:
    void loop() {
        for (;;) {
            BuildRequest req;
            bool precise = false;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || m_pending.has_value(); });
                if (m_stop) return;
                req = std::move(*m_pending);
                m_pending.reset();
                precise = m_precise;
                m_cancel = false;
            }
            BuildControl control;
            control.cancel = &m_cancel;
            if (m_started_ms.load() == 0) m_started_ms = now_ms();
            m_busy = true;
            Scene fast = build_scene(req, control, NumberType::Hardware);
            if (!m_cancel) {
                publish(std::make_shared<Scene>(std::move(fast)));
                m_started_ms = 0;
            }
            if (precise && req.plot_type != NumberType::Hardware && !m_cancel) {
                m_refining = true;
                Scene fine = build_scene(req, control, req.plot_type);
                m_refining = false;
                if (!m_cancel) publish(std::make_shared<Scene>(std::move(fine)));
            }
            m_busy = false;
            if (!m_cancel) m_started_ms = 0;
        }
    }

    void publish(std::shared_ptr<Scene> s) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done = std::move(s);
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<BuildRequest> m_pending;
    bool m_precise = false;
    bool m_stop = false;
    std::atomic<bool> m_cancel{ false };
    std::atomic<bool> m_refining{ false };
    std::atomic<bool> m_busy{ false };
    std::atomic<long long> m_started_ms{ 0 };
    std::shared_ptr<Scene> m_done;
    std::thread m_thread;
};

bool ask_to_end_frozen_app() {
    return ::arbor_ask_to_end("Arbor isn't responding", "Arbor has stopped responding. It may be stuck on something it's drawing.\n\nEnd Arbor now? Your graph was kept a moment ago and will be offered back the next time Arbor opens.\n\nYes ends Arbor; No keeps waiting.");
}

class Watchdog {
public:
    Watchdog() : m_thread([this] { loop(); }) {}

    ~Watchdog() {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
    }

    void beat() { m_last = Builder::now_ms(); }
    void pause(bool paused) {
        m_paused = paused;
        beat();
    }

private:
    void loop() {
        long long patience = 8000;
        while (!m_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (m_paused || m_last == 0) continue;
            const long long quiet = Builder::now_ms() - m_last;
            if (quiet < patience) continue;
            if (ask_to_end_frozen_app()) std::_Exit(3);
            beat();
            patience = 20000;
        }
    }

    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_paused{ false };
    std::atomic<long long> m_last{ 0 };
    std::thread m_thread;
};

class App {
public:
    App(grove::Project& project) : m_doc(project.document) {
        m_data = data_dir();
        std::error_code ec;
        fs::create_directories(m_data / "graphs", ec);
        fs::create_directories(m_data / "thumbs", ec);
        if (auto s = read_text(m_data / "settings.bark")) m_settings.load(*s);
        register_natives();
    }

    void install() {
        m_canvas = m_doc.find_by_id("plot");
        if (m_canvas) {
            grove::canvas_of(m_doc, *m_canvas);
            auto& ev = m_doc.events();
            ev.add_listener(m_canvas, "canvas_resize", [this](grove::Event&) { canvas_resized(); });
            ev.add_listener(m_canvas, "mouse_down", [this](grove::Event& e) { mouse_down(e); });
            ev.add_listener(m_canvas, "mouse_move", [this](grove::Event& e) { mouse_move(e); });
            ev.add_listener(nullptr, "mouse_up", [this](grove::Event& e) { mouse_up(e); });
            ev.add_listener(m_canvas, "wheel", [this](grove::Event& e) { wheel(e); });
            ev.add_listener(m_canvas, "double_click", [this](grove::Event&) {
                reset_view();
                changed_view(true);
            });
            ev.add_listener(m_canvas, "mouse_leave", [this](grove::Event&) { set_text("coords", hint_text()); });
        }
        m_doc.events().add_listener(nullptr, "input", [this](grove::Event& e) {
            if (!e.target || !e.has_edit_text || !m_graph) return;
            const std::string id = e.target->attribute_or("data-item", "");
            const std::string field = e.target->attribute_or("data-field", "");
            if (id.empty() || field.empty()) return;
            Item* it = m_graph->find_item(id);
            if (!it || it->field(field) == e.edit_text) return;
            snapshot("field:" + id + ":" + field);
            it->fields[field] = e.edit_text;
            if (field == "eq") update_eq_view(*it, e.edit_text);
            if (it->kind == "define" && field == "eq") update_eq_views();
            touched(true);
        });
        m_doc.events().set_interval(16.0, [this] { tick(); });
        m_doc.events().set_interval(2000.0, [this] { autosave_recovery(); });
    }

private:

    void native(const std::string& name, std::function<std::string(const std::vector<std::string>&)> fn) {
        grove::register_native(m_doc, "arbor." + name, std::move(fn));
    }

    void register_natives() {
        native("version", [](const std::vector<std::string>&) { return std::string(ARBOR_VERSION_STRING); });
        native("data_dir", [this](const std::vector<std::string>&) { return m_data.string(); });

        native("sections", [](const std::vector<std::string>&) {
            std::string out;
            for (const SectionInfo& s : sections()) out += s.id + "\t" + clean(s.title) + "\t" + clean(s.description) + "\n";
            return out;
        });
        native("kinds", [](const std::vector<std::string>& a) {
            const std::string section = arg(a, 0);
            std::string out;
            for (const KindInfo& k : kinds()) {
                if (k.id == "define" || (!section.empty() && k.section != section)) continue;
                out += k.id + "\t" + clean(k.title) + "\t" + clean(k.formula) + "\t" + clean(k.description) + "\t" + space_id(k.space) + "\n";
            }
            return out;
        });
        native("examples", [](const std::vector<std::string>& a) {
            const std::string section = arg(a, 0);
            std::string out;
            for (const Example& e : examples()) {
                if (!section.empty() && e.section != section) continue;
                out += e.id + "\t" + clean(e.title) + "\t" + clean(e.description) + "\n";
            }
            return out;
        });
        native("saved", [this](const std::vector<std::string>&) { return saved_list(); });

        native("new", [this](const std::vector<std::string>& a) {
            Graph g = new_graph(arg(a, 0));
            apply_default_bounds(g);
            start_graph(g, false);
            return std::string("ok");
        });
        native("open_example", [this](const std::vector<std::string>& a) {
            Graph g;
            if (!make_example(arg(a, 0), g)) return std::string("no such example");
            start_graph(g, false);
            return std::string("ok");
        });
        native("open", [this](const std::vector<std::string>& a) { return open_saved(arg(a, 0)); });
        native("open_file", [this](const std::vector<std::string>& a) { return open_file(arg(a, 0)); });
        native("save", [this](const std::vector<std::string>&) { return save_current(); });
        native("export_graph", [this](const std::vector<std::string>& a) {
            if (!m_graph) return std::string("no graph");
            return write_text(arg(a, 0), save_graph(*m_graph)) ? std::string("ok") : "can't write " + arg(a, 0);
        });
        native("export_png", [this](const std::vector<std::string>& a) { return export_png(arg(a, 0)); });
        native("close", [this](const std::vector<std::string>&) {
            close_graph();
            return std::string("ok");
        });
        native("delete", [this](const std::vector<std::string>& a) { return delete_saved(arg(a, 0)); });
        native("duplicate", [this](const std::vector<std::string>& a) { return duplicate_saved(arg(a, 0)); });
        native("rename", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            return rename_saved(arg(a, 0), arg(a, 1));
        });
        native("has_graph", [this](const std::vector<std::string>&) { return std::string(m_graph ? "1" : "0"); });
        native("dirty", [this](const std::vector<std::string>&) { return std::string(m_graph && m_dirty ? "1" : "0"); });
        native("is_saved", [this](const std::vector<std::string>&) { return std::string(m_graph && m_on_disk ? "1" : "0"); });
        native("title", [this](const std::vector<std::string>&) { return m_graph ? m_graph->title : std::string(); });
        native("set_title", [this](const std::vector<std::string>& a) {
            if (!m_graph) return std::string();
            std::string t = clean(arg(a, 0));
            if (t.empty()) t = "Untitled";
            if (t != m_graph->title) {
                snapshot("title");
                m_graph->title = t;
                touched(false);
            }
            return m_graph->title;
        });
        native("space", [this](const std::vector<std::string>&) { return std::string(m_graph ? space_id(m_graph->space) : "2d"); });
        native("recovery", [this](const std::vector<std::string>&) { return recovery_title(); });
        native("restore", [this](const std::vector<std::string>&) { return restore_recovery(); });
        native("drop_recovery", [this](const std::vector<std::string>&) {
            std::error_code ec;
            fs::remove(m_data / "recovery.bark", ec);
            return std::string("ok");
        });
        native("undo", [this](const std::vector<std::string>&) { return std::string(undo(true) ? "1" : "0"); });
        native("redo", [this](const std::vector<std::string>&) { return std::string(undo(false) ? "1" : "0"); });

        native("items", [this](const std::vector<std::string>&) {
            std::string out;
            if (!m_graph) return out;
            for (const Item& it : m_graph->items) out += it.id + "\t" + it.kind + "\n";
            return out;
        });
        native("item", [this](const std::vector<std::string>& a) { return item_text(arg(a, 0)); });
        native("kinds_here", [this](const std::vector<std::string>&) {
            std::string out;
            if (!m_graph) return out;
            for (const KindInfo* k : kinds_for_space(m_graph->space)) out += k->id + "\t" + clean(k->title) + " \xC2\xB7 " + clean(k->formula) + "\t" + k->section + "\n";
            return out;
        });
        native("add_item", [this](const std::vector<std::string>& a) {
            if (!m_graph) return std::string();
            snapshot("add");
            Item& it = m_graph->add_item(arg(a, 0));
            const std::string id = it.id;
            touched(true);
            return id;
        });
        native("remove_item", [this](const std::vector<std::string>& a) {
            if (!m_graph) return std::string();
            snapshot("remove");
            const bool ok = m_graph->remove_item(arg(a, 0));
            touched(true);
            return std::string(ok ? "1" : "0");
        });
        native("move_item", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            if (!m_graph) return std::string();
            snapshot("move");
            const bool ok = m_graph->move_item(arg(a, 0), std::atoi(arg(a, 1).c_str()));
            touched(true);
            return std::string(ok ? "1" : "0");
        });
        native("duplicate_item", [this](const std::vector<std::string>& a) {
            if (!m_graph) return std::string();
            const Item* src = m_graph->find_item(arg(a, 0));
            if (!src) return std::string();
            snapshot("duplicate");
            Item copy = *src;
            copy.id = "item-" + std::to_string(m_graph->next_item++);
            const auto at = std::find_if(m_graph->items.begin(), m_graph->items.end(), [&](const Item& i) { return i.id == src->id; });
            m_graph->items.insert(at + 1, copy);
            touched(true);
            return copy.id;
        });
        native("set_field", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            Item* it = m_graph ? m_graph->find_item(arg(a, 0)) : nullptr;
            if (!it) return std::string();
            const std::string value = a.size() > 2 ? a[2] : std::string();
            if (it->field(arg(a, 1)) == value) return std::string("same");
            snapshot("field:" + it->id + ":" + arg(a, 1));
            it->fields[arg(a, 1)] = value;
            if (arg(a, 1) == "eq") update_eq_views();
            touched(true);
            return std::string("ok");
        });
        native("set_param", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            return set_param(arg(a, 0), std::atof(arg(a, 1).c_str()), true);
        });
        native("play_param", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            if (arg(a, 1) == "1") {
                if (m_graph && m_graph->find_item(arg(a, 0))) snapshot("field:" + arg(a, 0) + ":at");
                m_param_playing.insert(arg(a, 0));
            } else {
                m_param_playing.erase(arg(a, 0));
            }
            return std::string("ok");
        });
        native("set_option", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            Item* it = m_graph ? m_graph->find_item(arg(a, 0)) : nullptr;
            if (!it) return std::string();
            snapshot("option");
            it->options[arg(a, 1)] = arg(a, 2) == "1";
            touched(true);
            return std::string("ok");
        });
        native("set_visible", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            Item* it = m_graph ? m_graph->find_item(arg(a, 0)) : nullptr;
            if (!it) return std::string();
            snapshot("visible");
            it->visible = arg(a, 1) == "1";
            touched(true);
            return std::string("ok");
        });
        native("set_expanded", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            Item* it = m_graph ? m_graph->find_item(arg(a, 0)) : nullptr;
            if (!it) return std::string();
            it->expanded = arg(a, 1) == "1";
            return std::string("ok");
        });
        native("set_color", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            Item* it = m_graph ? m_graph->find_item(arg(a, 0)) : nullptr;
            if (!it) return std::string();
            Rgba c;
            if (!parse_color(arg(a, 1), c)) return it->color;
            snapshot("color");
            it->color = color_hex(c).substr(0, 7);
            touched(true);
            return it->color;
        });
        native("next_color", [this](const std::vector<std::string>& a) {
            Item* it = m_graph ? m_graph->find_item(arg(a, 0)) : nullptr;
            if (!it) return std::string();
            snapshot("color");
            const auto& p = palette();
            auto pos = std::find(p.begin(), p.end(), it->color);
            it->color = pos == p.end() || pos + 1 == p.end() ? p.front() : *(pos + 1);
            touched(true);
            return it->color;
        });
        native("palette", [](const std::vector<std::string>&) {
            std::string out;
            for (const std::string& c : palette()) out += c + "\t";
            return out;
        });

        native("revision", [this](const std::vector<std::string>&) { return std::to_string(m_status_revision); });
        native("status", [this](const std::vector<std::string>& a) { return status_text(arg(a, 0)); });
        native("wanted", [this](const std::vector<std::string>&) {
            std::string out;
            if (!m_scene || !m_graph) return out;
            for (const std::string& n : m_scene->wanted_sliders) out += n + "\t";
            return out;
        });
        native("add_slider", [this](const std::vector<std::string>& a) { return add_slider(arg(a, 0)); });
        native("set_slider", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            return set_slider(arg(a, 0), std::atof(arg(a, 1).c_str()), true);
        });
        native("slider_range", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            return slider_range(arg(a, 0), arg(a, 1), arg(a, 2), arg(a, 3));
        });
        native("play", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            Slider& s = slider_for(arg(a, 0));
            s.playing = arg(a, 1) == "1";
            return std::string(s.playing ? "1" : "0");
        });
        native("info", [this](const std::vector<std::string>&) { return info_text(); });

        native("view", [this](const std::vector<std::string>& args) { return view_command(fields_of(args)); });
        native("view_state", [this](const std::vector<std::string>&) {
            if (!m_graph) return std::string();
            const Graph& g = *m_graph;
            return std::string(g.show_grid ? "1" : "0") + "\t" + (g.show_axes ? "1" : "0") + "\t" + (g.show_labels ? "1" : "0") + "\t" + (g.degrees ? "1" : "0") + "\t" + (g.view3.perspective ? "1" : "0") + "\t" + (g.view2.equal_axes ? "1" : "0");
        });

        native("setting", [this](const std::vector<std::string>& a) {
            const auto m = m_settings.to_map();
            auto it = m.find(arg(a, 0));
            return it == m.end() ? std::string() : it->second;
        });
        native("set_setting", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            if (!m_settings.set(arg(a, 0), arg(a, 1))) return std::string("unknown setting");
            write_text(m_data / "settings.bark", m_settings.save());
            if (m_graph) {
                request_build();
                m_render_wanted = true;
            }
            ++m_status_revision;
            return std::string("ok");
        });
        native("number_types", [](const std::vector<std::string>&) {
            std::string out;
            for (NumberType t : selectable_number_types()) {
                std::string about;
                switch (t) {
                    case NumberType::ShortShortFloat: about = "Walnut's 32-bit float in software (Fizmo). About 7 digits; its functions run on the CPU's double, so it's the quickest software type."; break;
                    case NumberType::ShortFloat: about = "Walnut's 64-bit float in software (Fizmo). The digits of a CPU double, computed exactly the way Walnut does; functions use double-double math."; break;
                    case NumberType::Float: about = "Walnut's 128-bit float, the same width as short short double (Fizmo). About 34 digits; trig and exp use quad-double math."; break;
                    case NumberType::LongFloat: about = "Walnut's 256-bit float, the same width as short double (Fizmo). About 71 digits; everything goes through BigFloat."; break;
                    case NumberType::LongLongFloat: about = "Walnut's 512-bit long long float, the same width as double (Fizmo). About 148 digits."; break;
                    case NumberType::LongDouble: about = "Walnut's 1024-bit long double (Fizmo). About 301 digits. Slow: best for readouts, not precise drawing."; break;
                    case NumberType::LongLongDouble: about = "Walnut's 2048-bit long long double (Fizmo). About 607 digits. The slowest by far."; break;
                    default: break;
                }
                out += std::string(number_type_id(t)) + "\t" + number_type_label(t) + "\t" + std::to_string(number_type_digits(t)) + "\t" + about + "\n";
            }
            return out;
        });
        native("library", [](const std::vector<std::string>&) { return library_text(); });
        native("busy", [this](const std::vector<std::string>&) {
            return std::to_string(static_cast<long long>(m_builder.busy_seconds())) + "\t" + (m_builder.cancelling() ? "1" : "0");
        });
        native("stop_build", [this](const std::vector<std::string>&) {
            m_builder.cancel();
            set_text("info", "Drawing stopped. Change something to draw again.");
            return std::string("ok");
        });
        native("quit_now", [this](const std::vector<std::string>&) {
            if (m_graph && m_dirty) write_text(m_data / "recovery.bark", save_graph(*m_graph));
            std::_Exit(0);
            return std::string();
        });
        native("watch", [this](const std::vector<std::string>& a) {
            m_watchdog.pause(arg(a, 0) == "0");
            return std::string("ok");
        });
        native("library_categories", [](const std::vector<std::string>&) {
            std::string out;
            for (const std::string& c : library_categories()) out += c + "\n";
            return out;
        });
        native("bounds_state", [this](const std::vector<std::string>&) { return bounds_state(); });
        native("set_bound", [this](const std::vector<std::string>& args) {
            const auto a = fields_of(args);
            return set_bound(arg(a, 0), arg(a, 1));
        });
        native("benchmark", [](const std::vector<std::string>&) { return benchmark(); });
        native("theme_vars", [this](const std::vector<std::string>&) { return theme_vars(); });
    }

    struct SavedInfo {
        std::string id;
        std::string title;
        std::string space;
        std::string modified;
        std::string thumb;
        std::string kinds;
    };

    std::vector<SavedInfo> saved_graphs() {
        std::vector<SavedInfo> out;
        std::error_code ec;
        for (const fs::directory_entry& e : fs::directory_iterator(m_data / "graphs", ec)) {
            if (e.path().extension() != ".bark") continue;
            auto text = read_text(e.path());
            if (!text) continue;
            Graph g;
            std::string err;
            if (!load_graph(*text, g, err)) continue;
            SavedInfo s;
            s.id = e.path().stem().string();
            s.title = g.title;
            s.space = space_id(g.space);
            s.modified = g.modified;
            s.thumb = thumb_for(s.id);
            std::vector<std::string> names;
            for (const Item& it : g.items) {
                if (it.kind == "define") continue;
                if (const KindInfo* k = find_kind(it.kind)) {
                    if (std::find(names.begin(), names.end(), k->title) == names.end()) names.push_back(k->title);
                }
            }
            for (std::size_t i = 0; i < names.size() && i < 3; ++i) s.kinds += (i ? ", " : "") + names[i];
            out.push_back(s);
        }
        std::sort(out.begin(), out.end(), [](const SavedInfo& a, const SavedInfo& b) { return a.modified > b.modified; });
        return out;
    }

    std::string saved_list() {
        std::string out;
        for (const SavedInfo& s : saved_graphs()) out += s.id + "\t" + clean(s.title) + "\t" + s.space + "\t" + clean(s.modified) + "\t" + s.thumb + "\t" + clean(s.kinds) + "\n";
        return out;
    }

    std::string thumb_for(const std::string& id) {
        std::error_code ec;
        std::string best;
        for (const fs::directory_entry& e : fs::directory_iterator(m_data / "thumbs", ec)) {
            const std::string name = e.path().filename().string();
            if (name.rfind(id + "-", 0) == 0 && e.path().extension() == ".png") {
                if (best.empty() || name > fs::path(best).filename().string()) best = e.path().string();
            }
        }
        return best;
    }

    static bool positive_number(const std::string& text, double& out) {
        if (text.find_first_not_of(" \t") == std::string::npos) return false;
        double v = 0.0;
        std::string err;
        ParseResult r = parse_expression(text);
        if (!r.ok) return false;
        Program p = compile(r.statement.lhs, {}, false, err);
        if (!p.valid) return false;
        auto ev = make_evaluator(p, NumberType::Hardware);
        v = ev->real1({});
        if (!(std::isfinite(v) && v > 0.0)) return false;
        out = v;
        return true;
    }

    void apply_default_bounds(Graph& g) {
        if (g.space == Space::Plane) {
            g.view2.bound_xmin = m_settings.bound_2d_xmin;
            g.view2.bound_xmax = m_settings.bound_2d_xmax;
            g.view2.bound_ymin = m_settings.bound_2d_ymin;
            g.view2.bound_ymax = m_settings.bound_2d_ymax;
        } else {
            double r = 0.0;
            if (positive_number(m_settings.box_3d, r) && std::fabs(g.view3.range - 5.0) < 1e-12) g.view3.range = r;
            double z = 0.0;
            if (positive_number(m_settings.zbox_3d, z)) {
                g.view3.zrange = z;
                g.view3.auto_z = false;
            }
        }
    }

    void start_graph(const Graph& g, bool on_disk) {
        m_param_playing.clear();
        m_graph = std::make_unique<Graph>(g);
        m_on_disk = on_disk;
        m_dirty = false;
        m_scene.reset();
        m_undo.clear();
        m_redo.clear();
        m_last_snapshot_key.clear();
        ++m_status_revision;
        request_build();
        m_render_wanted = true;
    }

    std::string open_saved(const std::string& id) {
        auto text = read_text(m_data / "graphs" / (id + ".bark"));
        if (!text) return "can't find that graph";
        Graph g;
        std::string err;
        if (!load_graph(*text, g, err)) return err;
        g.id = id;
        start_graph(g, true);
        return "ok";
    }

    std::string open_file(const std::string& path) {
        auto text = read_text(path);
        if (!text) return "can't read " + path;
        Graph g;
        std::string err;
        if (!load_graph(*text, g, err)) return err;
        g.id = make_id();
        start_graph(g, false);
        m_dirty = true;
        return "ok";
    }

    std::string save_current() {
        if (!m_graph) return "no graph";
        m_graph->modified = now_text();
        if (m_graph->id.empty()) m_graph->id = make_id();
        if (!write_text(m_data / "graphs" / (m_graph->id + ".bark"), save_graph(*m_graph))) return "couldn't write the graph file";
        write_thumbnail();
        m_on_disk = true;
        m_dirty = false;
        std::error_code ec;
        fs::remove(m_data / "recovery.bark", ec);
        return "ok";
    }

    void write_thumbnail() {
        if (!m_graph) return;
        const int w = 360, h = 240;
        BuildRequest req = make_request(w, h);
        BuildControl control;
        Scene s = build_scene(req, control, NumberType::Hardware);
        RenderOptions o = render_options(*m_graph, 2);
        const double k = static_cast<double>(w) / std::max(1, m_canvas_w);
        o.view2.scale = m_graph->view2.scale * std::max(0.2, k);
        o.view2.scale_y = m_graph->view2.scale_y * std::max(0.2, k);
        req.graph.view2 = o.view2;
        s = build_scene(req, control, NumberType::Hardware);
        o.labels = false;
        Image img;
        img.resize(w, h);
        render_scene(s, o, img);
        std::error_code ec;
        for (const fs::directory_entry& e : fs::directory_iterator(m_data / "thumbs", ec)) {
            if (e.path().filename().string().rfind(m_graph->id + "-", 0) == 0) fs::remove(e.path(), ec);
        }
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        write_png((m_data / "thumbs" / (m_graph->id + "-" + std::to_string(stamp) + ".png")).string(), img.px.data(), img.w, img.h);
    }

    std::string export_png(const std::string& path) {
        if (!m_graph || !m_scene) return "nothing to export";
        const int w = std::max(200, m_canvas_w) * 2;
        const int h = std::max(150, m_canvas_h) * 2;
        BuildRequest req = make_request(w, h);
        req.graph.view2.scale *= 2.0;
        req.graph.view2.scale_y *= 2.0;
        BuildControl control;
        Scene s = build_scene(req, control, NumberType::Hardware);
        RenderOptions o = render_options(*m_graph, 2);
        o.view2 = req.graph.view2;
        o.ui_scale = m_settings.ui_scale * 2.0;
        Image img;
        img.resize(w, h);
        render_scene(s, o, img);
        std::string p = path;
        if (fs::path(p).extension() != ".png") p += ".png";
        return write_png(p, img.px.data(), img.w, img.h) ? std::string("ok") : "can't write " + p;
    }

    void close_graph() {
        m_param_playing.clear();
        m_graph.reset();
        m_scene.reset();
        m_dirty = false;
        m_on_disk = false;
        std::error_code ec;
        fs::remove(m_data / "recovery.bark", ec);
        ++m_status_revision;
    }

    std::string delete_saved(const std::string& id) {
        std::error_code ec;
        fs::remove(m_data / "graphs" / (id + ".bark"), ec);
        for (const fs::directory_entry& e : fs::directory_iterator(m_data / "thumbs", ec)) {
            if (e.path().filename().string().rfind(id + "-", 0) == 0) fs::remove(e.path(), ec);
        }
        if (m_graph && m_graph->id == id) m_on_disk = false;
        return "ok";
    }

    std::string duplicate_saved(const std::string& id) {
        auto text = read_text(m_data / "graphs" / (id + ".bark"));
        if (!text) return "can't find that graph";
        Graph g;
        std::string err;
        if (!load_graph(*text, g, err)) return err;
        g.id = make_id();
        g.title += " (copy)";
        g.modified = now_text();
        if (!write_text(m_data / "graphs" / (g.id + ".bark"), save_graph(g))) return "couldn't write the copy";
        const std::string thumb = thumb_for(id);
        if (!thumb.empty()) {
            std::error_code ec;
            fs::copy_file(thumb, m_data / "thumbs" / (g.id + "-0.png"), ec);
        }
        return "ok";
    }

    std::string rename_saved(const std::string& id, const std::string& title) {
        const fs::path p = m_data / "graphs" / (id + ".bark");
        auto text = read_text(p);
        if (!text) return "can't find that graph";
        Graph g;
        std::string err;
        if (!load_graph(*text, g, err)) return err;
        g.title = clean(title).empty() ? "Untitled" : clean(title);
        if (!write_text(p, save_graph(g))) return "couldn't write the graph";
        if (m_graph && m_graph->id == id) m_graph->title = g.title;
        return "ok";
    }

    void autosave_recovery() {
        if (!m_graph || !m_dirty) return;
        if (m_recovery_revision == m_change_count) return;
        m_recovery_revision = m_change_count;
        write_text(m_data / "recovery.bark", save_graph(*m_graph));
    }

    std::string recovery_title() {
        auto text = read_text(m_data / "recovery.bark");
        if (!text) return std::string();
        Graph g;
        std::string err;
        if (!load_graph(*text, g, err)) return std::string();
        return g.title.empty() ? std::string("Untitled") : g.title;
    }

    std::string restore_recovery() {
        auto text = read_text(m_data / "recovery.bark");
        if (!text) return "nothing to restore";
        Graph g;
        std::string err;
        if (!load_graph(*text, g, err)) return err;
        std::error_code ec;
        const bool exists = !g.id.empty() && fs::exists(m_data / "graphs" / (g.id + ".bark"), ec);
        if (g.id.empty()) g.id = make_id();
        start_graph(g, exists);
        m_dirty = true;
        return "ok";
    }

    void snapshot(const std::string& key) {
        if (!m_graph) return;
        const auto now = std::chrono::steady_clock::now();
        if (key == m_last_snapshot_key && key.rfind("field:", 0) == 0 && now - m_last_snapshot_time < std::chrono::seconds(2)) {
            m_last_snapshot_time = now;
            return;
        }
        m_last_snapshot_key = key;
        m_last_snapshot_time = now;
        m_undo.push_back(save_graph(*m_graph));
        if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
        m_redo.clear();
    }

    bool undo(bool back) {
        if (!m_graph) return false;
        auto& from = back ? m_undo : m_redo;
        auto& to = back ? m_redo : m_undo;
        if (from.empty()) return false;
        to.push_back(save_graph(*m_graph));
        Graph g;
        std::string err;
        if (!load_graph(from.back(), g, err)) return false;
        from.pop_back();
        g.id = m_graph->id;
        *m_graph = g;
        m_last_snapshot_key.clear();
        touched(true);
        return true;
    }

    static bool complex_kind(const std::string& kind) {
        return kind == "domain" || kind == "conformal" || kind == "complex_surface" || kind == "complex_curve";
    }

    static std::string math_prefix(const std::string& kind) {
        static const std::pair<const char*, const char*> prefixes[] = {
            { "parametric", "(x, y) = " }, { "curve_value", "f(x, y) = " }, { "vector_field", "F(x, y) = " }, { "contour", "f(x, y) = " },
            { "slope_field", "(dy)/(dx) = " }, { "curve3d", "r(t) = " }, { "param_surface", "r(u, v) = " }, { "curve_value3d", "f(x, y, z) = " },
            { "vector_field3d", "F(x, y, z) = " }, { "domain", "f(z) = " }, { "conformal", "w = " }, { "complex_surface", "f(z) = " },
            { "complex_curve", "f(t) = " }, { "riemann", "z = " }, { "transform2d", "(x, y) = " }, { "transform3d", "(x, y, z) = " }
        };
        for (const auto& p : prefixes) {
            if (kind == p.first) return p.second;
        }
        return std::string();
    }

    std::string eq_math(const Item& it, const std::string& text) const {
        if (it.kind == "points" || it.kind == "points3d") return std::string();
        const ParseResult r = parse_statement_as_typed(text);
        if (!r.ok) return std::string();
        std::string m = display_mathtext(r.statement);
        if (r.statement.relation == Relation::None) m = math_prefix(it.kind) + m;
        return m;
    }

    bool branched_expr(const ExprPtr& e, bool complex, const std::map<std::string, ExprPtr>& defs, int depth) const {
        if (!e || depth > 8) return false;
        if (uses_branches(e, complex)) return true;
        if (e->op == Op::Call) {
            const auto d = defs.find(e->text);
            if (d != defs.end() && branched_expr(d->second, complex, defs, depth + 1)) return true;
        }
        for (const ExprPtr& a : e->args) {
            if (branched_expr(a, complex, defs, depth + 1)) return true;
        }
        return false;
    }

    bool eq_branched(const Item& it, const std::string& text) const {
        if (!m_graph || it.kind == "define" || it.kind == "riemann" || it.kind == "points" || it.kind == "points3d") return false;
        const ParseResult r = parse_statement_as_typed(text);
        if (!r.ok) return false;
        std::map<std::string, ExprPtr> defs;
        for (const Item& other : m_graph->items) {
            if (other.kind != "define") continue;
            const ParseResult d = parse_statement_as_typed(other.field("eq"));
            if (d.ok && d.statement.is_function_definition) defs[d.statement.defines] = d.statement.rhs;
        }
        const bool complex = complex_kind(it.kind);
        if (branched_expr(r.statement.lhs, complex, defs, 0) || branched_expr(r.statement.rhs, complex, defs, 0)) return true;
        const ParseResult path = parse_statement_as_typed(it.field("path"));
        return path.ok && branched_expr(path.statement.lhs, false, defs, 0);
    }

    void update_eq_view(const Item& it, const std::string& text) {
        bool changed = false;
        if (grove::Node* m = m_doc.find_by_id("math-" + it.id)) {
            const std::string math = eq_math(it, text);
            if (!math.empty()) {
                m->set_inner_text(math);
                grove::remove_class(m_doc, *m, "stale");
            } else if (text.empty()) {
                m->set_inner_text(std::string());
            } else {
                grove::add_class(m_doc, *m, "stale");
            }
            changed = true;
        }
        if (grove::Node* b = m_doc.find_by_id("branch-" + it.id)) {
            const bool branched = eq_branched(it, text);
            if (branched) grove::remove_class(m_doc, *b, "hidden");
            else grove::add_class(m_doc, *b, "hidden");
            if (grove::Node* row = m_doc.find_by_id("eqrow-" + it.id)) {
                if (branched) grove::add_class(m_doc, *row, "with-branch");
                else grove::remove_class(m_doc, *row, "with-branch");
            }
            changed = true;
        }
        if (changed) m_doc.touch();
    }

    void update_eq_views() {
        if (!m_graph) return;
        for (const Item& it : m_graph->items) update_eq_view(it, it.field("eq"));
    }

    struct DomainSpec {
        std::string var;
        std::string lo;
        std::string hi;
        bool range = false;
    };

    static bool limit_closed_option(const Item& item, const std::string& field) {
        const auto it = item.options.find(field + "_closed");
        return it == item.options.end() ? true : it->second;
    }

    static std::vector<DomainSpec> domain_specs(const Item& item) {
        const std::string& k = item.kind;
        if (k == "function") {
            std::string t = item.field("eq");
            const std::size_t a = t.find_first_not_of(" \t");
            bool in_y = false;
            if (a != std::string::npos && t[a] == 'x') {
                const std::size_t b = t.find_first_not_of(" \t", a + 1);
                in_y = b != std::string::npos && t[b] == '=';
            }
            return { in_y ? DomainSpec{ "y", "ymin", "ymax", false } : DomainSpec{ "x", "xmin", "xmax", false } };
        }
        if (k == "implicit" || k == "surface") return { { "x", "xmin", "xmax", false }, { "y", "ymin", "ymax", false } };
        if (k == "implicit3d") return { { "x", "xmin", "xmax", false }, { "y", "ymin", "ymax", false }, { "z", "zmin", "zmax", false } };
        if (k == "polar") return { { "\xCE\xB8", "min", "max", true } };
        if (k == "parametric" || k == "curve3d") return { { "t", "min", "max", true } };
        if (k == "revolution") {
            std::string t = item.field("eq");
            const std::size_t a = t.find_first_not_of(" \t");
            std::string var = "x";
            if (a != std::string::npos && (t[a] == '<' || t.compare(a, 3, "\xE2\x9F\xA8") == 0)) var = "t";
            else if (a != std::string::npos && t[a] == 'x') {
                const std::size_t b = t.find_first_not_of(" \t", a + 1);
                if (b != std::string::npos && t[b] == '=') var = "y";
            }
            return { { var, "min", "max", true } };
        }
        return {};
    }

    std::string bounds_state() {
        if (!m_graph) return std::string();
        const Graph& g = *m_graph;
        if (g.space == Space::Plane) {
            return "2d\t" + clean(g.view2.bound_xmin) + "\t" + clean(g.view2.bound_xmax) + "\t" + clean(g.view2.bound_ymin) + "\t" + clean(g.view2.bound_ymax);
        }
        const View3D& v = g.view3;
        const double ry = v.range_y > 0.0 ? v.range_y : v.range;
        std::string z1, z2;
        if (!v.auto_z) {
            z1 = trim_number(v.target.z - v.zrange, 8);
            z2 = trim_number(v.target.z + v.zrange, 8);
        }
        return "3d\t" + trim_number(v.target.x - v.range, 8) + "\t" + trim_number(v.target.x + v.range, 8) + "\t" + trim_number(v.target.y - ry, 8) + "\t" + trim_number(v.target.y + ry, 8) + "\t" + z1 + "\t" + z2;
    }

    std::string set_bound(const std::string& key, const std::string& value) {
        if (!m_graph) return std::string();
        Graph& g = *m_graph;
        snapshot("bounds:" + key);
        if (g.space == Space::Plane) {
            if (key == "x-min") g.view2.bound_xmin = value;
            else if (key == "x-max") g.view2.bound_xmax = value;
            else if (key == "y-min") g.view2.bound_ymin = value;
            else if (key == "y-max") g.view2.bound_ymax = value;
            else if (key == "clear") g.view2.bound_xmin = g.view2.bound_xmax = g.view2.bound_ymin = g.view2.bound_ymax = std::string();
            else return "unknown bound";
            changed_view(true);
            touched(true);
            return "ok";
        }
        View3D& v = g.view3;
        auto number = [](const std::string& text, double& out) {
            if (text.find_first_not_of(" \t") == std::string::npos) return false;
            ParseResult r = parse_expression(text);
            if (!r.ok) return false;
            std::string err;
            Program p = compile(r.statement.lhs, {}, false, err);
            if (!p.valid) return false;
            const double x = make_evaluator(p, NumberType::Hardware)->real1({});
            if (!std::isfinite(x)) return false;
            out = x;
            return true;
        };
        double x = 0.0;
        const bool have = number(value, x);
        const double ry = v.range_y > 0.0 ? v.range_y : v.range;
        auto set_pair = [&](double& center, double& half, bool lo_side) {
            double a = center - half;
            double b = center + half;
            (lo_side ? a : b) = x;
            if (!(b > a)) return false;
            center = 0.5 * (a + b);
            half = 0.5 * (b - a);
            return true;
        };
        bool ok = true;
        if (key == "x-min" || key == "x-max") {
            if (!have) return "write a number";
            double y_half = ry;
            ok = set_pair(v.target.x, v.range, key == "x-min");
            if (ok && v.range_y <= 0.0) v.range_y = y_half;
        } else if (key == "y-min" || key == "y-max") {
            if (!have) return "write a number";
            double half = ry;
            ok = set_pair(v.target.y, half, key == "y-min");
            if (ok) v.range_y = half;
        } else if (key == "z-min" || key == "z-max") {
            if (!have) {
                v.auto_z = true;
            } else {
                if (v.auto_z) {
                    v.zrange = v.range;
                    v.auto_z = false;
                }
                ok = set_pair(v.target.z, v.zrange, key == "z-min");
            }
        } else if (key == "clear") {
            const Graph fresh = new_graph(g.items.empty() ? std::string("surface") : g.items.back().kind);
            v.target = Vec3{ 0.0, 0.0, 0.0 };
            v.range = fresh.space == Space::Solid ? fresh.view3.range : 5.0;
            v.range_y = 0.0;
            v.auto_z = true;
        } else {
            return "unknown bound";
        }
        if (!ok) return "the lower end has to be below the upper end";
        if (std::fabs(v.range_y - v.range) < 1e-12 * v.range) v.range_y = 0.0;
        changed_view(true);
        touched(true);
        return "ok";
    }

    std::string item_text(const std::string& id) {
        const Item* it = m_graph ? m_graph->find_item(id) : nullptr;
        if (!it) return std::string();
        const KindInfo* k = find_kind(it->kind);
        std::string out = it->kind + "\t" + it->color + "\t" + (it->visible ? "1" : "0") + "\t" + clean(k ? k->title : it->kind) + "\t" + clean(k ? k->formula : "") + "\t" + (it->expanded ? "1" : "0") + "\n";
        if (!k) return out;
        out += "M\teq\t-\t" + clean(eq_math(*it, it->field("eq"))) + "\n";
        out += "B\tbranch\t" + std::string(eq_branched(*it, it->field("eq")) ? "1" : "0") + "\t" + clean(it->field("branch").empty() ? std::string("0") : it->field("branch")) + "\n";
        const auto value_of = [it](const std::string& id) { return it->field(id); };
        for (const FieldSpec& f : k->fields) {
            if (!shown_for(f.show_if, value_of)) continue;
            if (!f.choices.empty()) {
                std::string choices;
                for (const std::string& c : f.choices) choices += (choices.empty() ? "" : "|") + clean(c);
                out += "F\t" + f.id + "\t" + clean(f.label) + "\t" + clean(it->field(f.id)) + "\t" + clean(f.hint) + "\t4\t" + choices + "\n";
                continue;
            }
            std::string value = it->field(f.id);
            if (it->kind.rfind("pde", 0) == 0 && (f.id.rfind("eq", 0) == 0 || f.id.rfind("guess", 0) == 0)) value = brace_subscripts(value);
            out += "F\t" + f.id + "\t" + clean(f.label) + "\t" + clean(value) + "\t" + clean(f.hint) + "\t" + (f.id == "eq" ? "1" : f.wide || f.id == "path" ? "2" : math_field(f.id) ? "3" : "0") + "\n";
        }
        for (const DomainSpec& d : domain_specs(*it)) {
            out += "D\t" + d.var + "\t" + d.lo + "\t" + clean(it->field(d.lo)) + "\t" + (limit_closed_option(*it, d.lo) ? "1" : "0") + "\t" + d.hi + "\t" + clean(it->field(d.hi)) + "\t" + (limit_closed_option(*it, d.hi) ? "1" : "0") + "\t" + (d.range ? "1" : "0") + "\n";
        }
        for (const OptionSpec& o : k->options) {
            if (shown_for(o.show_if, value_of)) out += "O\t" + o.id + "\t" + clean(o.label) + "\t" + (it->option(o.id) ? "1" : "0") + "\t" + clean(o.hint) + "\n";
        }
        return out;
    }

    static std::string brace_subscripts(const std::string& text) {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if ((ch == 'u' || ch == 'v' || ch == 'w') && i + 2 < text.size() && text[i + 1] == '_') {
                std::size_t j = i + 2;
                while (j < text.size() && (text[j] == 'x' || text[j] == 'y' || text[j] == 'z' || text[j] == 't')) ++j;
                if (j - (i + 2) >= 2) {
                    out += text.substr(i, 2) + "{" + text.substr(i + 2, j - (i + 2)) + "}";
                    i = j - 1;
                    continue;
                }
            }
            out += ch;
        }
        return out;
    }

    static bool math_field(const std::string& id) {
        static const char* const ids[] = { "angle", "at", "cx", "cy", "upper", "min", "max", "xmin", "xmax", "ymin", "ymax", "zmin", "zmax", "umin", "umax", "vmin", "vmax", "x0", "y0", "x1", "y1", "g", "tmax", "init", "init2", "start", "tmin" };
        for (const char* m : ids) {
            if (id == m) return true;
        }
        return false;
    }

    bool slider_of(const Item& it, std::string& name, double& value) {
        if (it.kind != "define") return false;
        ParseResult r = parse_statement(it.field("eq"));
        if (!r.ok || !r.statement.is_value_definition) return false;
        const ExprPtr& rhs = r.statement.rhs;
        if (rhs->op == Op::Number) value = rhs->value;
        else if (rhs->op == Op::Neg && rhs->args[0]->op == Op::Number) value = -rhs->args[0]->value;
        else return false;
        name = r.statement.defines;
        return true;
    }

    Slider& slider_for(const std::string& name) {
        if (Slider* s = m_graph->find_slider(name)) return *s;
        Slider s;
        s.name = name;
        s.min = -10.0;
        s.max = 10.0;
        m_graph->sliders.push_back(s);
        return m_graph->sliders.back();
    }

    std::string status_text(const std::string& id) {
        std::string out;
        if (!m_graph) return out;
        const Item* item = m_graph->find_item(id);
        if (!item) return out;
        std::string name;
        double value = 0.0;
        if (slider_of(*item, name, value)) {
            Slider& s = slider_for(name);
            if (value < s.min) s.min = std::floor(value);
            if (value > s.max) s.max = std::ceil(value);
            out += "slider\t" + name + "\t" + trim_number(value, 10) + "\t" + trim_number(s.min, 10) + "\t" + trim_number(s.max, 10) + "\t" + trim_number(slider_step(s), 10) + "\t" + (s.playing ? "1" : "0") + "\n";
        }
        if (!m_scene) return out;
        for (const ItemStatus& st : m_scene->status) {
            if (st.id != id) continue;
            if (!st.error.empty()) out += "error\t" + clean(st.error) + "\n";
            if (!st.mathtext.empty()) out += "math\t" + clean(pretty(st.mathtext)) + "\n";
            if (!st.note.empty() && st.note != "slider") out += "note\t" + clean(st.note) + "\n";
            for (const Readout& r : st.readouts) {
                const bool value = m_settings.readout_values && !r.value.empty();
                const bool symbol = m_settings.readout_symbols && !r.symbol.empty();
                if (!value && !symbol) continue;
                out += "readout\t" + clean(r.label) + "\t" + (value ? clean(pretty(r.value)) : std::string()) + "\t" + (symbol ? clean(r.symbol) : std::string()) + "\n";
            }
            if (!st.legend_map.empty()) out += "legend\t" + st.legend_min + "\t" + st.legend_max + "\t" + st.legend_map + "\n";
            if (st.has_param && param_free(*item)) {
                out += "param\t" + clean(st.param_label) + "\t" + trim_number(st.param_value, 10) + "\t" + trim_number(st.param_min, 10) + "\t" + trim_number(st.param_max, 10) + "\t" + (m_param_playing.count(id) ? "1" : "0") + "\t" + trim_number((st.param_max - st.param_min) / 1000.0, 6) + "\n";
            }
        }
        return out;
    }

    static bool param_free(const Item& item) {
        std::string t = item.field("at");
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
        if (t.empty() || t == "pi" || t == "e" || t == "tau") return true;
        if (!std::isalpha(static_cast<unsigned char>(t[0]))) return true;
        for (char ch : t) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return true;
        }
        return false;
    }

    const ItemStatus* scene_status(const std::string& id) const {
        if (!m_scene) return nullptr;
        for (const ItemStatus& st : m_scene->status) {
            if (st.id == id) return &st;
        }
        return nullptr;
    }

    std::string set_param(const std::string& id, double value, bool record) {
        Item* it = m_graph ? m_graph->find_item(id) : nullptr;
        if (!it || !std::isfinite(value)) return std::string();
        const std::string text = trim_number(value, 8);
        if (it->field("at") == text) return std::string("same");
        if (record) snapshot("field:" + id + ":at");
        it->fields["at"] = text;
        touched(true);
        return text;
    }

    void advance_params(double dt) {
        if (!m_graph) return;
        for (auto i = m_param_playing.begin(); i != m_param_playing.end();) {
            const ItemStatus* st = scene_status(*i);
            Item* it = m_graph->find_item(*i);
            if (!it || !st || !st->has_param) {
                ++i;
                continue;
            }
            double v = 0.0;
            char* end = nullptr;
            const std::string cur = it->field("at");
            v = std::strtod(cur.c_str(), &end);
            if (end == cur.c_str() || !std::isfinite(v)) v = st->param_value;
            const double span = st->param_max - st->param_min;
            double next = v + span * dt / 8.0;
            if (next > st->param_max || next < st->param_min) next = st->param_min;
            set_param(*i, next, false);
            ++i;
        }
        ++m_status_revision;
    }

    static std::string pretty(std::string s) {
        static const std::vector<std::pair<std::string, std::string>> names = {
            { "\\theta\\", "\xCE\xB8" }, { "\\phi\\", "\xCF\x86" }, { "\\rho\\", "\xCF\x81" }, { "\\pi\\", "\xCF\x80" }, { "\\tau\\", "\xCF\x84" },
            { "\\alpha\\", "\xCE\xB1" }, { "\\beta\\", "\xCE\xB2" }, { "\\gamma\\", "\xCE\xB3" }, { "\\lambda\\", "\xCE\xBB" }, { "\\omega\\", "\xCF\x89" },
            { "\\mu\\", "\xCE\xBC" }, { "\\sigma\\", "\xCF\x83" }, { "\\epsilon\\", "\xCE\xB5" }, { "\\delta\\", "\xCE\xB4" },
        };
        for (const auto& [from, to] : names) {
            std::size_t p = 0;
            while ((p = s.find(from, p)) != std::string::npos) {
                s.replace(p, from.size(), to);
                p += to.size();
            }
        }
        return s;
    }

    std::string add_slider(const std::string& name) {
        if (!m_graph || name.empty()) return std::string();
        snapshot("slider");
        Item it;
        it.id = "item-" + std::to_string(m_graph->next_item++);
        it.kind = "define";
        it.color = palette()[0];
        it.fields["eq"] = name + " = 1";
        m_graph->items.insert(m_graph->items.begin(), it);
        slider_for(name);
        touched(true);
        return it.id;
    }

    std::string set_slider(const std::string& name, double value, bool record) {
        if (!m_graph) return std::string();
        for (Item& it : m_graph->items) {
            std::string n;
            double v = 0.0;
            if (!slider_of(it, n, v) || n != name) continue;
            Slider& s = slider_for(name);
            const double step = slider_step(s);
            if (step > 0) value = s.min + std::round((value - s.min) / step) * step;
            const int digits = step > 0 ? std::clamp(static_cast<int>(std::ceil(-std::log10(step))) + 1, 1, 12) : 6;
            if (record) snapshot("field:" + it.id + ":eq");
            it.fields["eq"] = name + " = " + format_double(value, std::max(digits, 3));
            touched(true);
            return it.fields["eq"];
        }
        return std::string();
    }

    std::string slider_range(const std::string& name, const std::string& lo, const std::string& hi, const std::string& step) {
        if (!m_graph) return std::string();
        Slider& s = slider_for(name);
        char* end = nullptr;
        const double a = std::strtod(lo.c_str(), &end);
        if (end != lo.c_str()) s.min = a;
        const double b = std::strtod(hi.c_str(), &end);
        if (end != hi.c_str()) s.max = b;
        const double st = std::strtod(step.c_str(), &end);
        s.step = end != step.c_str() ? std::max(0.0, st) : 0.0;
        if (s.max < s.min) std::swap(s.min, s.max);
        if (s.max == s.min) s.max = s.min + 1.0;
        m_dirty = true;
        ++m_status_revision;
        return "ok";
    }

    void advance_sliders(double dt) {
        if (!m_graph) return;
        bool any = false;
        for (Slider& s : m_graph->sliders) {
            if (!s.playing) continue;
            for (const Item& it : m_graph->items) {
                std::string n;
                double v = 0.0;
                if (!slider_of(it, n, v) || n != s.name) continue;
                const double span = s.max - s.min;
                double next = v + span * dt / 8.0;
                if (next > s.max) next = s.min;
                const double step = s.step;
                s.step = 0.0;
                set_slider(s.name, next, false);
                s.step = step;
                any = true;
                break;
            }
        }
        if (any) ++m_status_revision;
    }

    BuildRequest make_request(int w, int h) {
        BuildRequest req;
        req.graph = *m_graph;
        req.width = w;
        req.height = h;
        const NumberType t = number_type_from_id(m_settings.number_type);
        req.plot_type = t;
        req.value_type = t;
        req.quality = m_settings.quality;
        req.theme = m_settings.canvas_theme();
        req.symbols = m_settings.readout_symbols;
        req.revision = ++m_build_revision;
        return req;
    }

    void request_build() {
        if (!m_graph) return;
        const int w = std::max(50, m_canvas_w);
        const int h = std::max(50, m_canvas_h);
        m_builder.submit(make_request(w, h), m_settings.precise_plot);
        m_last_request = std::chrono::steady_clock::now();
    }

    RenderOptions render_options(const Graph& g, int supersample) {
        RenderOptions o;
        o.space = g.space;
        o.view2 = g.view2;
        o.view3 = g.view3;
        o.theme = m_settings.canvas_theme();
        o.grid = g.show_grid;
        o.axes = g.show_axes;
        o.labels = g.show_labels;
        o.ui_scale = m_settings.ui_scale;
        o.supersample = supersample;
        return o;
    }

    void render(bool quick) {
        if (!m_canvas || m_canvas_w <= 0 || m_canvas_h <= 0) return;
        Image img;
        img.resize(m_canvas_w, m_canvas_h);
        if (!m_graph) {
            img.clear(m_settings.canvas_theme().background);
        } else {
            static const Scene empty;
            const Scene& s = m_scene ? *m_scene : empty;
            const int ss = m_graph->space == Space::Solid && m_settings.smooth_3d && !quick ? 2 : 1;
            render_scene(s, render_options(*m_graph, ss), img);
        }
        grove::set_canvas_pixels(m_doc, *m_canvas, static_cast<unsigned>(img.w), static_cast<unsigned>(img.h), std::move(img.px));
        m_rendered_smooth = !quick;
    }

    void layout_home() {
        const double zoom = std::max(0.25, m_settings.ui_scale);
        const double width = m_doc.host().viewport_width / zoom;
        if (width <= 0.0 || (width == m_layout_width && zoom == m_layout_zoom)) return;
        m_layout_width = width;
        m_layout_zoom = zoom;
        grove::Node* body = m_doc.find_by_id("app");
        if (!body) return;
        const double inner = std::min(width, 1800.0) - 64.0;
        const double big = std::clamp(width / 1280.0, 1.0, 1.45);
        const double gap = std::round(12.0 * big);
        const double target = 230.0 * big;
        const int cols = std::clamp(static_cast<int>((inner + gap) / (target + gap)), 1, 7);
        const double card = std::floor((inner - gap * (cols - 1)) / cols) - 1.0;
        auto px = [](double v) { return trim_number(std::round(v * 10.0) / 10.0, 6) + "px"; };
        const std::pair<const char*, std::string> vars[] = {
            { "--card-w", px(card) },
            { "--card-gap", px(gap) },
            { "--card-pad", px(14.0 * big) },
            { "--card-title", px(14.0 * big) },
            { "--card-text", px(12.0 * big) },
            { "--card-formula", px(13.0 * big) },
            { "--tab-font", px(13.0 * big) },
            { "--tab-pad", px(7.0 * big) + " " + px(18.0 * big) },
            { "--home-title", px(20.0 * big) },
            { "--thumb-h", px((card - 18.0) * 2.0 / 3.0) },
        };
        for (const auto& [name, value] : vars) grove::set_inline_style(m_doc, *body, name, value);
    }

    void tick() {
        m_watchdog.beat();
        layout_home();
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - m_last_tick).count();
        m_last_tick = now;
        if (m_graph) {
            bool playing = !m_param_playing.empty();
            for (const Slider& s : m_graph->sliders) playing = playing || s.playing;
            if (playing && now - m_last_play > std::chrono::milliseconds(40)) {
                const double step = std::min(0.1, std::chrono::duration<double>(now - m_last_play).count());
                advance_sliders(step);
                if (!m_param_playing.empty()) advance_params(step);
                m_last_play = now;
            } else if (!playing) {
                m_last_play = now;
            }
        }
        (void)dt;
        if (std::shared_ptr<Scene> s = m_builder.take()) {
            m_scene = std::move(s);
            ++m_status_revision;
            m_render_wanted = true;
            set_text("info", info_text());
        }
        const bool interacting = m_dragging || now - m_last_interaction < std::chrono::milliseconds(180);
        if (m_render_wanted) {
            m_render_wanted = false;
            render(interacting);
        } else if (!interacting && !m_rendered_smooth && m_graph && m_graph->space == Space::Solid) {
            render(false);
        }
        if (m_builder.refining() != m_was_refining) {
            m_was_refining = m_builder.refining();
            set_text("info", info_text());
        }
    }

    void touched(bool rebuild) {
        m_dirty = true;
        ++m_change_count;
        ++m_status_revision;
        if (rebuild) request_build();
    }

    void changed_view(bool rebuild) {
        if (!m_graph) return;
        m_dirty = true;
        m_last_interaction = std::chrono::steady_clock::now();
        m_render_wanted = true;
        if (rebuild) request_build();
    }

    void canvas_resized() {
        unsigned w = 0, h = 0;
        if (!m_canvas || !grove::canvas_size(m_doc, *m_canvas, w, h)) return;
        if (static_cast<int>(w) == m_canvas_w && static_cast<int>(h) == m_canvas_h) return;
        m_canvas_w = static_cast<int>(w);
        m_canvas_h = static_cast<int>(h);
        m_render_wanted = true;
        request_build();
    }

    void set_text(const char* id, const std::string& text) {
        if (grove::Node* n = m_doc.find_by_id(id)) n->set_inner_text(text);
    }

    std::string info_text() {
        if (!m_graph) return std::string();
        const NumberType t = number_type_from_id(m_settings.number_type);
        std::string s = m_graph->space == Space::Solid ? "3D" : "2D";
        if (m_scene) {
            s += " \xC2\xB7 plotted with " + std::string(number_type_label(m_scene->plotted_with)) + " in " + format_double(m_scene->build_ms, 3) + " ms";
        }
        if (m_builder.refining()) s += " \xC2\xB7 refining with " + std::string(number_type_label(t)) + "\xE2\x80\xA6";
        s += " \xC2\xB7 values in " + std::string(number_type_label(t));
        if (t != NumberType::Hardware) s += " (" + std::to_string(number_type_digits(t)) + " digits)";
        return s;
    }

    std::string hint_text() const {
        if (!m_graph) return std::string();
        if (m_graph->space == Space::Solid) return "Drag to turn \xC2\xB7 Shift+drag or right-drag to move \xC2\xB7 Wheel to zoom \xC2\xB7 Shift+wheel to widen the domain \xC2\xB7 Double-click to reset";
        return "Drag to move \xC2\xB7 Wheel to zoom \xC2\xB7 Shift+wheel zooms y only \xC2\xB7 Double-click to reset";
    }

    grove::CanvasRect rect() const {
        return m_canvas ? grove::canvas_rect(m_doc, *m_canvas) : grove::CanvasRect{};
    }

    void mouse_down(grove::Event& e) {
        if (!m_graph) return;
        m_dragging = true;
        m_drag_button = e.button;
        m_drag_shift = (e.modifiers & grove::ModifierShift) != 0;
        m_last_x = e.x;
        m_last_y = e.y;
        m_last_interaction = std::chrono::steady_clock::now();
    }

    void mouse_up(grove::Event&) {
        if (!m_dragging) return;
        m_dragging = false;
        if (m_graph && m_graph->space == Space::Solid && m_panned) request_build();
        m_panned = false;
        m_last_interaction = std::chrono::steady_clock::now();
    }

    void mouse_move(grove::Event& e) {
        if (!m_graph) return;
        const grove::CanvasRect r = rect();
        if (!m_dragging) {
            hover(e.x - r.x, e.y - r.y);
            return;
        }
        const double dx = e.x - m_last_x;
        const double dy = e.y - m_last_y;
        m_last_x = e.x;
        m_last_y = e.y;
        if (dx == 0 && dy == 0) return;
        Graph& g = *m_graph;
        if (g.space == Space::Plane) {
            g.view2.cx -= dx / g.view2.scale;
            g.view2.cy += dy / (g.view2.equal_axes ? g.view2.scale : g.view2.scale_y);
            changed_view(true);
            hover(e.x - r.x, e.y - r.y);
            return;
        }
        const bool pan = m_drag_button == 2 || m_drag_button == 3 || m_drag_shift || (e.modifiers & grove::ModifierShift);
        if (pan) {
            const double f = std::min(m_canvas_w, m_canvas_h) * 0.28 * g.view3.zoom;
            const double k = g.view3.range / std::max(1.0, f);
            const double cy = std::cos(g.view3.yaw), sy = std::sin(g.view3.yaw);
            const double sp = std::max(0.25, std::fabs(std::sin(g.view3.pitch)));
            g.view3.target.x -= (dx * cy + dy / sp * sy) * k;
            g.view3.target.y -= (-dx * sy + dy / sp * cy) * k * (g.view3.range_y > 0.0 ? g.view3.range_y / g.view3.range : 1.0);
            m_panned = true;
            changed_view(false);
            if (std::chrono::steady_clock::now() - m_last_request > std::chrono::milliseconds(60)) request_build();
            return;
        }
        g.view3.yaw += dx * 0.009;
        g.view3.pitch = std::clamp(g.view3.pitch + dy * 0.009, -1.55, 1.55);
        changed_view(false);
    }

    void wheel(grove::Event& e) {
        if (!m_graph) return;
        e.default_prevented = true;
        const double steps = std::clamp(e.delta_y, -10.0, 10.0);
        if (steps == 0) return;
        Graph& g = *m_graph;
        const bool shift = (e.modifiers & grove::ModifierShift) != 0;
        const double k = std::pow(1.15, steps);
        if (g.space == Space::Plane) {
            const grove::CanvasRect r = rect();
            const double px = e.x - r.x, py = e.y - r.y;
            const WorldBox2 b = world_box(g.view2, m_canvas_w, m_canvas_h);
            const double wx = b.xmin + px / m_canvas_w * (b.xmax - b.xmin);
            const double wy = b.ymax - py / m_canvas_h * (b.ymax - b.ymin);
            if (g.view2.equal_axes && !shift) {
                g.view2.scale = std::clamp(g.view2.scale * k, 1e-6, 1e12);
                g.view2.scale_y = g.view2.scale;
            } else {
                if (g.view2.equal_axes) {
                    g.view2.equal_axes = false;
                    g.view2.scale_y = g.view2.scale;
                }
                if (shift) g.view2.scale_y = std::clamp(g.view2.scale_y * k, 1e-6, 1e12);
                else {
                    g.view2.scale = std::clamp(g.view2.scale * k, 1e-6, 1e12);
                    g.view2.scale_y = std::clamp(g.view2.scale_y * k, 1e-6, 1e12);
                }
            }
            const WorldBox2 nb = world_box(g.view2, m_canvas_w, m_canvas_h);
            const double nwx = nb.xmin + px / m_canvas_w * (nb.xmax - nb.xmin);
            const double nwy = nb.ymax - py / m_canvas_h * (nb.ymax - nb.ymin);
            g.view2.cx += wx - nwx;
            g.view2.cy += wy - nwy;
            changed_view(true);
            return;
        }
        if (shift) {
            g.view3.range = std::clamp(g.view3.range / k, 1e-6, 1e9);
            if (g.view3.range_y > 0.0) g.view3.range_y = std::clamp(g.view3.range_y / k, 1e-6, 1e9);
            if (!g.view3.auto_z) g.view3.zrange = std::clamp(g.view3.zrange / k, 1e-6, 1e9);
            changed_view(true);
        } else {
            g.view3.zoom = std::clamp(g.view3.zoom * k, 0.1, 20.0);
            changed_view(false);
        }
    }

    void hover(double px, double py) {
        if (!m_graph) return;
        if (m_graph->space == Space::Solid || m_canvas_w <= 0) {
            set_text("coords", hint_text());
            return;
        }
        const WorldBox2 b = world_box(m_graph->view2, m_canvas_w, m_canvas_h);
        const double x = b.xmin + px / m_canvas_w * (b.xmax - b.xmin);
        const double y = b.ymax - py / m_canvas_h * (b.ymax - b.ymin);
        const double digits = std::max(2.0, std::ceil(std::log10(std::max(1.0, m_graph->view2.scale))) + 2.0);
        std::string s = "x = " + format_double(x, static_cast<int>(digits) + 1) + "   y = " + format_double(y, static_cast<int>(digits) + 1);
        if (m_scene) {
            for (Probe& p : m_scene->probes) {
                if (p.at) s += "   \xC2\xB7   " + p.at(x, y);
            }
        }
        set_text("coords", s);
    }

    void reset_view() {
        if (!m_graph) return;
        const Graph fresh = new_graph(m_graph->items.empty() ? std::string("function") : m_graph->items.back().kind);
        if (m_graph->space == Space::Plane) {
            const View2D old = m_graph->view2;
            m_graph->view2 = View2D{};
            m_graph->view2.bound_xmin = old.bound_xmin;
            m_graph->view2.bound_xmax = old.bound_xmax;
            m_graph->view2.bound_ymin = old.bound_ymin;
            m_graph->view2.bound_ymax = old.bound_ymax;
            m_graph->view2.scale = fresh.space == Space::Plane ? fresh.view2.scale : 40.0;
        } else {
            const bool persp = m_graph->view3.perspective;
            m_graph->view3 = fresh.space == Space::Solid ? fresh.view3 : View3D{};
            m_graph->view3.perspective = persp;
        }
    }

    std::string view_command(const std::vector<std::string>& a) {
        if (!m_graph) return std::string();
        Graph& g = *m_graph;
        const std::string cmd = arg(a, 0);
        const bool on = arg(a, 1) == "1";
        bool rebuild = true;
        if (cmd == "reset") reset_view();
        else if (cmd == "zoom_in" || cmd == "zoom_out") {
            const double k = cmd == "zoom_in" ? 1.4 : 1.0 / 1.4;
            if (g.space == Space::Plane) {
                g.view2.scale *= k;
                g.view2.scale_y *= k;
            } else {
                g.view3.zoom = std::clamp(g.view3.zoom * k, 0.1, 20.0);
                rebuild = false;
            }
        } else if (cmd == "grid") g.show_grid = on;
        else if (cmd == "axes") g.show_axes = on;
        else if (cmd == "labels") g.show_labels = on;
        else if (cmd == "degrees") g.degrees = on;
        else if (cmd == "perspective") {
            g.view3.perspective = on;
            rebuild = false;
        } else if (cmd == "equal") {
            g.view2.equal_axes = on;
            if (on) g.view2.scale_y = g.view2.scale;
        } else if (cmd == "top" || cmd == "front" || cmd == "side") {
            g.view3.pitch = cmd == "top" ? 1.55 : 0.0;
            g.view3.yaw = cmd == "side" ? -1.5707963 : 0.0;
            rebuild = false;
        } else return "unknown view command";
        changed_view(rebuild);
        return "ok";
    }

    std::string theme_vars() {
        std::string bg, panel, card, line, text, muted, accent, hover;
        if (m_settings.theme == "light") {
            bg = "#f4f4f7"; panel = "#ffffff"; card = "#fbfbfd"; line = "#dcdce4"; text = "#26262e"; muted = "#6b6b7a"; accent = "#2563eb"; hover = "#ececf2";
        } else if (m_settings.theme == "custom") {
            bg = m_settings.bg; panel = m_settings.panel; text = m_settings.text; accent = m_settings.accent;
            Rgba b, p, t;
            if (!parse_color(bg, b)) b = rgba(30, 30, 46);
            if (!parse_color(panel, p)) p = b;
            if (!parse_color(text, t)) t = rgba(205, 214, 244);
            card = color_hex(mix(p, t, 0.04)).substr(0, 7);
            line = color_hex(mix(p, t, 0.16)).substr(0, 7);
            muted = color_hex(mix(t, b, 0.4)).substr(0, 7);
            hover = color_hex(mix(p, t, 0.09)).substr(0, 7);
        } else {
            bg = "#1e1e2e"; panel = "#181825"; card = "#202031"; line = "#313244"; text = "#cdd6f4"; muted = "#9399b2"; accent = "#89b4fa"; hover = "#2a2b3c";
        }
        const std::string error = m_settings.theme == "light" ? "#d20f39" : "#f38ba8";
        return "--error\t" + error + "\n--bg\t" + bg + "\n--panel\t" + panel + "\n--card\t" + card + "\n--line\t" + line + "\n--text\t" + text + "\n--muted\t" + muted + "\n--accent\t" + accent + "\n--hover\t" + hover + "\n";
    }

    static std::string benchmark() {
        ParseResult r = parse_expression("sin(x)*x + exp(-x^2/4)");
        std::string out;
        if (!r.ok) return out;
        std::string err;
        Program p = compile(r.statement.lhs, { "x" }, false, err);
        if (!p.valid) return out;
        for (NumberType t : selectable_number_types()) {
            auto ev = make_evaluator(p, t);
            const std::size_t n = t == NumberType::ShortShortFloat || t == NumberType::ShortFloat ? 20000 : t == NumberType::LongDouble || t == NumberType::LongLongDouble ? 60 : 400;
            std::vector<double> xs(n), ys(n);
            for (std::size_t i = 0; i < n; ++i) xs[i] = -5.0 + 10.0 * static_cast<double>(i) / static_cast<double>(n);
            const double* in[1] = { xs.data() };
            const auto t0 = std::chrono::steady_clock::now();
            ev->real(n, in, ys.data());
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / static_cast<double>(n);
            const std::string shown = ns >= 1000.0 ? format_double(ns / 1000.0, 3) + " \xC2\xB5s" : format_double(ns, 3) + " ns";
            out += std::string(number_type_id(t)) + "\t" + shown + "\n";
        }
        return out;
    }

    grove::Document& m_doc;
    double m_layout_width = 0.0;
    double m_layout_zoom = 0.0;
    fs::path m_data;
    Settings m_settings;
    std::unique_ptr<Graph> m_graph;
    bool m_dirty = false;
    bool m_on_disk = false;
    std::shared_ptr<Scene> m_scene;
    Builder m_builder;
    Watchdog m_watchdog;
    grove::Node* m_canvas = nullptr;
    int m_canvas_w = 0;
    int m_canvas_h = 0;
    bool m_render_wanted = false;
    bool m_rendered_smooth = true;
    bool m_was_refining = false;
    std::uint64_t m_status_revision = 1;
    std::uint64_t m_build_revision = 0;
    std::uint64_t m_change_count = 0;
    std::uint64_t m_recovery_revision = 0;
    std::vector<std::string> m_undo;
    std::vector<std::string> m_redo;
    std::string m_last_snapshot_key;
    std::chrono::steady_clock::time_point m_last_snapshot_time;
    std::chrono::steady_clock::time_point m_last_tick = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_last_play = std::chrono::steady_clock::now();
    std::set<std::string> m_param_playing;
    std::chrono::steady_clock::time_point m_last_interaction;
    std::chrono::steady_clock::time_point m_last_request;
    bool m_dragging = false;
    bool m_drag_shift = false;
    bool m_panned = false;
    int m_drag_button = 0;
    double m_last_x = 0.0;
    double m_last_y = 0.0;
};

} // namespace

#if !defined(__ANDROID__)
int bundle_ui(const std::string& output, bool portable) {
    const std::string ui = find_ui();
    if (ui.empty()) {
        std::cerr << "arbor: can't find the UI (arbor.grove) to bundle; set ARBOR_UI_DIR to the folder that holds it\n";
        return 1;
    }
    grove::AppBuildOptions options;
    options.output = output;
    options.stub = grove::executable_path();
    options.tools = grove::default_script_tools(ui);
    options.portable = portable;
    grove::Diagnostics diagnostics;
    const grove::AppBuildResult result = grove::build_app(ui, options, diagnostics, &std::cout);
    if (!result.ok) {
        diagnostics.print(std::cerr);
        std::cerr << "arbor: couldn't bundle the UI into " << output << "\n";
        return 1;
    }
    std::cout << "arbor: wrote " << result.output << " (" << result.files << " files, " << result.modules << " compiled scripts)\n";
    return 0;
}

int main(int argc, char** argv) {
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version") {
            std::cout << "arbor " << ARBOR_VERSION_STRING << "\n";
            return 0;
        }
        if (a == "--help") {
            std::cout << "usage: arbor [--check] [--bundle OUTPUT [--portable]]\n\n"
                         "  --check          load the window and its script without opening it, then exit\n"
                         "  --bundle OUTPUT  write a copy of arbor with the window and its compiled script built in\n"
                         "  --portable       with --bundle, link the script with the static C++ runtime (when the compiler has one)\n\n"
                         "Set ARBOR_UI_DIR to use a different UI folder and ARBOR_DATA_DIR to keep graphs somewhere else.\n";
            return 0;
        }
        if (a == "--bundle") {
            if (i + 1 >= argc) {
                std::cerr << "arbor: --bundle needs an output path\n";
                return 2;
            }
            bool portable = false;
            for (int j = 1; j < argc; ++j) portable = portable || std::string(argv[j]) == "--portable";
            return bundle_ui(argv[i + 1], portable);
        }
        if (a == "--check") check = true;
    }

    grove::LoadedApp loaded;
    grove::Project from_files;
    grove::Project* project = nullptr;
    grove::runtime::RunOptions options;
    std::optional<grove::Bundle> bundle;
    if (env("ARBOR_UI_DIR").empty()) bundle = grove::read_embedded_bundle(grove::executable_path());
    if (bundle) {
        std::string error;
        if (!grove::load_app(*bundle, loaded, error)) {
            std::cerr << "arbor: " << error << "\n";
            return 1;
        }
        project = &loaded.project;
        options.window_id = loaded.window_id;
        options.prebuilt = &loaded.scripts;
        if (check) {
            std::cout << "arbor " << ARBOR_VERSION_STRING << ": built-in window ok, " << loaded.scripts.runnable_count() << " compiled script(s)\n";
            return loaded.scripts.ok() ? 0 : 1;
        }
    } else {
        const std::string ui = find_ui();
        if (ui.empty()) {
            std::cerr << "arbor: can't find the UI (arbor.grove); set ARBOR_UI_DIR to the folder that holds it\n";
            return 1;
        }
        from_files = grove::load_project(ui);
        if (!from_files.diagnostics.ok()) {
            from_files.diagnostics.print(std::cerr);
            return 1;
        }
        if (check) {
            std::cout << "arbor " << ARBOR_VERSION_STRING << ": window ok (" << ui << ")\n";
            return 0;
        }
        project = &from_files;
        options.tools = grove::default_script_tools(ui);
        options.tools.cache_dir = (data_dir() / "script-cache").string();
    }
    auto app = std::make_unique<App>(*project);
    app->install();
    return grove::runtime::run(*project, options);
}
#endif

#if defined(__ANDROID__)
namespace {

int show_start_error(fizmo::android::App& app, const std::string& message) {
    fizmo::android::log(fizmo::android::LogLevel::Error, "arbor", message);
    fizmo::graphics::Renderer renderer(app.surface());
    renderer.bind();
    fizmo::android::Event e;
    while (app.running()) {
        bool redraw = false;
        while (app.poll(e, 250)) {
            if (e.type == fizmo::android::EventType::Back) app.finish();
            redraw = true;
        }
        if (!redraw || !renderer.is_bound()) continue;
        const float k = app.density();
        const fizmo::android::Insets in = app.content_insets();
        const int x = in.left + static_cast<int>(24 * k);
        const int y = in.top + static_cast<int>(32 * k);
        const unsigned w = app.surface().width() > static_cast<unsigned>(x * 2) ? app.surface().width() - static_cast<unsigned>(x * 2) : app.surface().width();
        renderer.begin_frame();
        renderer.clear(fizmo::colors::Color(30, 30, 36));
        renderer.draw_text(x, y, w, static_cast<unsigned>(60 * k), std::string("Arbor couldn't start"), fizmo::text::TextStyle(22.0 * k, fizmo::colors::Color(243, 139, 168)));
        renderer.draw_text(x, y + static_cast<int>(48 * k), w, app.surface().height(), message, fizmo::text::TextStyle(15.0 * k, fizmo::colors::Color(205, 214, 244)));
        renderer.present();
    }
    return 1;
}

} // namespace

int fizmo_android_main(fizmo::android::App& app) {
    fizmo::android::redirect_stdio("arbor");
    const std::string data = app.internal_data_path().empty() ? std::string("/data/local/tmp") : app.internal_data_path();
    std::error_code ec;
    fs::create_directories(fs::path(data) / "data", ec);
    setenv("HOME", data.c_str(), 1);
    setenv("XDG_DATA_HOME", (fs::path(data) / "share").string().c_str(), 1);
    setenv("XDG_CONFIG_HOME", (fs::path(data) / "config").string().c_str(), 1);
    setenv("XDG_CACHE_HOME", (fs::path(data) / "cache").string().c_str(), 1);
    setenv("TMPDIR", app.cache_path().c_str(), 1);
    setenv("ARBOR_DATA_DIR", (fs::path(data) / "data").string().c_str(), 1);

    const std::optional<std::string> text = app.assets().read("grove/app.bundle");
    if (!text) return show_start_error(app, "this APK has no grove/app.bundle asset; build it again with grove build");
    std::optional<grove::Bundle> bundle = grove::parse_bundle(*text);
    if (!bundle) return show_start_error(app, "grove/app.bundle is damaged; build Arbor again");

    grove::AppLoadOptions options;
    options.root = data;
    options.cache_dir = app.cache_path();
    options.module_dir = app.native_library_dir();
    grove::LoadedApp loaded;
    std::string error;
    if (!grove::load_app(*bundle, loaded, error, options)) return show_start_error(app, error);

    auto shell = std::make_unique<App>(loaded.project);
    shell->install();
    grove::runtime::RunOptions run;
    run.window_id = loaded.window_id;
    run.scripts = true;
    return grove::runtime::run_android(loaded.project, app, &loaded.scripts, run);
}
#endif // __ANDROID__
