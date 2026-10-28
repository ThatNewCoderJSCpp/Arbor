#ifndef ARBOR_RENDER_HPP
#define ARBOR_RENDER_HPP

#include "graph.hpp"
#include "scene.hpp"
#include "vec.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace arbor {

struct Image {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> px;

    void resize(int width, int height);
    void clear(Rgba c);
    void blend(int x, int y, Rgba c, double coverage);
    Rgba at(int x, int y) const;
};

struct RenderOptions {
    Space space = Space::Plane;
    View2D view2;
    View3D view3;
    Theme theme;
    bool grid = true;
    bool axes = true;
    bool labels = true;
    double ui_scale = 1.0;
    int supersample = 1;
    bool box = true;
    std::string highlight;
    std::vector<Dot2> marks;
};

void render_scene(const Scene& scene, const RenderOptions& options, Image& out);

struct Camera3 {
    double yaw = 0.0;
    double pitch = 0.0;
    double zoom = 1.0;
    bool perspective = true;
    Vec3 target;
    double range = 5.0;
    double range_y = 0.0;
    double zmin = -5.0;
    double zmax = 5.0;
    int width = 800;
    int height = 600;

    Vec3 normalize(Vec3 world) const;
    Vec3 view(Vec3 n) const;
    bool project(Vec3 world, double& sx, double& sy, double& depth) const;
};

Camera3 make_camera(const View3D& v, const Scene& scene, int width, int height);

void draw_text(Image& img, double x, double y, const std::string& text, double size, Rgba color, int align = -1);
double text_width(const std::string& text, double size);

std::vector<std::uint8_t> downsample_rgba(const Image& img, int width, int height);

} // namespace arbor

#endif // ARBOR_RENDER_HPP
