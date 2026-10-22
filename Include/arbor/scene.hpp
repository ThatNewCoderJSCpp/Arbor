#ifndef ARBOR_SCENE_HPP
#define ARBOR_SCENE_HPP

#include "graph.hpp"
#include "program.hpp"
#include "vec.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace arbor {

struct Stroke2 {
    std::vector<Vec2> pts;
    std::vector<Rgba> colors;
    Rgba color;
    float width = 2.5f;
    bool dashed = false;
};

struct Arrow2 {
    Vec2 from;
    Vec2 to;
    Rgba color;
    float width = 2.0f;
    bool head = true;
};

struct Dot2 {
    Vec2 at;
    Rgba color;
    float radius = 5.0f;
    bool hollow = false;
    std::string label;
};

struct Raster2 {
    double xmin = 0.0;
    double xmax = 1.0;
    double ymin = 0.0;
    double ymax = 1.0;
    int w = 0;
    int h = 0;
    std::vector<Rgba> px;
};

struct Tri2 {
    Vec2 a;
    Vec2 b;
    Vec2 c;
    Rgba color;
};

struct Inset2 {
    std::string title;
    std::string xlabel;
    std::vector<Vec2> pts;
    std::vector<Rgba> colors;
    Rgba color;
    double marker = 0.0;
    bool has_marker = false;
};

struct Text2 {
    Vec2 at;
    std::string text;
    Rgba color;
};

struct Scene2 {
    std::vector<Raster2> rasters;
    std::vector<Tri2> tris;
    std::vector<Stroke2> strokes;
    std::vector<Arrow2> arrows;
    std::vector<Dot2> dots;
    std::vector<Inset2> insets;
    std::vector<Text2> texts;
    bool clipped = false;
    double clip_xmin = -1e300;
    double clip_xmax = 1e300;
    double clip_ymin = -1e300;
    double clip_ymax = 1e300;
};

struct Mesh3 {
    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;
    std::vector<Rgba> col;
    std::vector<std::uint32_t> tri;
    double alpha = 1.0;
    bool lit = true;
};

struct Line3 {
    std::vector<Vec3> pts;
    std::vector<Rgba> colors;
    Rgba color;
    float width = 2.5f;
    bool dashed = false;
    bool on_top = false;
};

struct Arrow3 {
    Vec3 from;
    Vec3 to;
    Rgba color;
    float width = 2.5f;
    std::string label;
};

struct Dot3 {
    Vec3 at;
    Rgba color;
    float radius = 5.0f;
    std::string label;
};

struct Scene3 {
    std::vector<Mesh3> meshes;
    std::vector<Line3> lines;
    std::vector<Arrow3> arrows;
    std::vector<Dot3> dots;
    double zmin = 0.0;
    double zmax = 0.0;
    bool has_z = false;
    std::string axis_x;
    std::string axis_y;
    std::string axis_z;
};

struct Readout {
    std::string label;
    std::string value;
    std::string symbol;
};

struct ItemStatus {
    std::string id;
    std::string error;
    std::string note;
    std::string mathtext;
    std::vector<Readout> readouts;
    Rgba swatch;
    std::string legend_min;
    std::string legend_max;
    std::string legend_map;
    bool has_param = false;
    std::string param_label;
    double param_min = 0.0;
    double param_max = 1.0;
    double param_value = 0.0;
};

struct Theme {
    Rgba background = rgba(30, 30, 46);
    Rgba grid_minor = rgba(255, 255, 255, 18);
    Rgba grid_major = rgba(255, 255, 255, 40);
    Rgba axis = rgba(205, 214, 244, 170);
    Rgba text = rgba(205, 214, 244, 220);
    Rgba box = rgba(205, 214, 244, 90);
    Rgba accent = rgba(137, 180, 250);
    bool dark = true;
};

struct BuildRequest {
    Graph graph;
    int width = 800;
    int height = 600;
    NumberType plot_type = NumberType::Hardware;
    NumberType value_type = NumberType::Hardware;
    int quality = 2;
    Theme theme;
    std::uint64_t revision = 0;
    bool symbols = true;
};

struct Probe {
    std::string item;
    std::function<std::string(double, double)> at;
};

struct Scene {
    Space space = Space::Plane;
    std::vector<Probe> probes;
    Scene2 s2;
    Scene3 s3;
    std::vector<ItemStatus> status;
    std::vector<std::string> wanted_sliders;
    std::uint64_t revision = 0;
    View2D built_view;
    int built_width = 0;
    int built_height = 0;
    NumberType plotted_with = NumberType::Hardware;
    bool precise_pass = false;
    double build_ms = 0.0;
};

struct BuildControl {
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(double)> progress;
    bool cancelled() const { return cancel && cancel->load(); }
};

Scene build_scene(const BuildRequest& request, const BuildControl& control, NumberType geometry_type);

struct WorldBox2 {
    double xmin = -10.0;
    double xmax = 10.0;
    double ymin = -10.0;
    double ymax = 10.0;
};

WorldBox2 world_box(const View2D& v, int width, int height);
std::vector<std::string> split_top_level(const std::string& text, char sep);

} // namespace arbor

#endif // ARBOR_SCENE_HPP
