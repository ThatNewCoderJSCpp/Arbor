#include "arbor/examples.hpp"
#include "arbor/graph.hpp"
#include "arbor/png.hpp"
#include "arbor/render.hpp"
#include "arbor/scene.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace arbor;

namespace {

int usage() {
    std::cerr << "usage: arbor-render GRAPH.bark|--example ID [-o OUT.png] [-w WIDTH] [-h HEIGHT]\n"
                 "                    [--type hardware|short-short-float|short-float|float|long-float|\n                     long-long-float|long-double|long-long-double] [--light]\n"
                 "       arbor-render --examples [DIR]   render every example into DIR\n"
                 "       arbor-render --list             list the examples\n";
    return 2;
}

Theme light_theme() {
    Theme t;
    t.background = rgba(250, 250, 252);
    t.grid_minor = rgba(0, 0, 0, 14);
    t.grid_major = rgba(0, 0, 0, 34);
    t.axis = rgba(40, 40, 50, 190);
    t.text = rgba(40, 40, 50, 230);
    t.box = rgba(40, 40, 50, 80);
    t.accent = rgba(37, 99, 235);
    t.dark = false;
    return t;
}

bool render_graph(const Graph& g, const std::string& out, int w, int h, NumberType type, const Theme& theme, bool quiet) {
    BuildRequest req;
    req.graph = g;
    req.width = w;
    req.height = h;
    req.plot_type = type;
    req.value_type = type;
    req.quality = 2;
    req.theme = theme;
    BuildControl control;
    const auto t0 = std::chrono::steady_clock::now();
    Scene scene = build_scene(req, control, type);
    const auto t1 = std::chrono::steady_clock::now();
    RenderOptions o;
    o.space = g.space;
    o.view2 = g.view2;
    o.view3 = g.view3;
    o.theme = theme;
    o.grid = g.show_grid;
    o.axes = g.show_axes;
    o.labels = g.show_labels;
    o.supersample = 2;
    Image img;
    img.resize(w, h);
    render_scene(scene, o, img);
    const auto t2 = std::chrono::steady_clock::now();
    if (!quiet) {
        std::cout << out << ": built in " << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms, drawn in " << std::chrono::duration<double, std::milli>(t2 - t1).count() << " ms\n";
        for (std::size_t i = 0; i < scene.status.size(); ++i) {
            const ItemStatus& st = scene.status[i];
            if (!st.error.empty()) std::cout << "  " << g.items[i].kind << ": error: " << st.error << "\n";
            if (!st.mathtext.empty()) std::cout << "  " << g.items[i].kind << ": " << st.mathtext << "\n";
            for (const Readout& r : st.readouts) std::cout << "    " << r.label << " = " << r.value << "\n";
        }
        for (const std::string& n : scene.wanted_sliders) std::cout << "  wants a value for " << n << "\n";
    }
    return write_png(out, img.px.data(), img.w, img.h);
}

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string example;
    std::string out = "graph.png";
    std::string dir;
    bool all = false;
    int w = 900;
    int h = 640;
    NumberType type = NumberType::Hardware;
    Theme theme;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "-o") out = next();
        else if (a == "-w") w = std::stoi(next());
        else if (a == "-h") h = std::stoi(next());
        else if (a == "--type") {
            const std::string id = next();
            type = id == "hardware" ? NumberType::Hardware : number_type_from_id(id);
        }
        else if (a == "--light") theme = light_theme();
        else if (a == "--example") example = next();
        else if (a == "--list") {
            for (const Example& e : examples()) std::cout << e.id << "  [" << e.section << "] " << e.title << "\n";
            return 0;
        } else if (a == "--examples") {
            all = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') dir = next();
        } else if (a == "--help" || a == "-?") return usage();
        else input = a;
    }
    if (all) {
        if (dir.empty()) dir = ".";
        bool ok = true;
        for (const Example& e : examples()) {
            Graph g;
            make_example(e.id, g);
            ok = render_graph(g, dir + "/" + e.id + ".png", w, h, type, theme, false) && ok;
        }
        return ok ? 0 : 1;
    }
    Graph g;
    if (!example.empty()) {
        if (!make_example(example, g)) {
            std::cerr << "arbor-render: no example called " << example << "\n";
            return 1;
        }
    } else if (!input.empty()) {
        std::ifstream f(input, std::ios::binary);
        if (!f) {
            std::cerr << "arbor-render: can't read " << input << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        if (!load_graph(ss.str(), g, err)) {
            std::cerr << "arbor-render: " << input << ": " << err << "\n";
            return 1;
        }
    } else {
        return usage();
    }
    return render_graph(g, out, w, h, type, theme, false) ? 0 : 1;
}
