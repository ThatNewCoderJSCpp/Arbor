#ifndef ARBOR_GRAPH_HPP
#define ARBOR_GRAPH_HPP

#include "kinds.hpp"
#include "vec.hpp"

#include <map>
#include <string>
#include <vector>

namespace arbor {

struct Item {
    std::string id;
    std::string kind;
    std::map<std::string, std::string> fields;
    std::map<std::string, bool> options;
    std::string color;
    bool visible = true;
    bool expanded = true;

    std::string field(const std::string& name) const;
    bool option(const std::string& name) const;
};

struct Slider {
    std::string name;
    double value = 1.0;
    double min = -10.0;
    double max = 10.0;
    double step = 0.0;
    bool playing = false;
    bool from_definition = false;
};

struct View2D {
    double cx = 0.0;
    double cy = 0.0;
    double scale = 40.0;
    bool equal_axes = true;
    double scale_y = 40.0;
    std::string bound_xmin;
    std::string bound_xmax;
    std::string bound_ymin;
    std::string bound_ymax;
};

struct View3D {
    double yaw = -0.7;
    double pitch = 0.45;
    double zoom = 1.0;
    Vec3 target{ 0.0, 0.0, 0.0 };
    double range = 5.0;
    double range_y = 0.0;
    double zrange = 5.0;
    bool auto_z = true;
    bool perspective = true;
};

struct Graph {
    std::string id;
    std::string title;
    Space space = Space::Plane;
    std::vector<Item> items;
    std::vector<Slider> sliders;
    View2D view2;
    View3D view3;
    std::string created;
    std::string modified;
    bool show_grid = true;
    bool show_axes = true;
    bool show_labels = true;
    bool degrees = false;
    int next_item = 1;

    Item* find_item(const std::string& id);
    const Item* find_item(const std::string& id) const;
    Slider* find_slider(const std::string& name);
    Item& add_item(const std::string& kind);
    bool remove_item(const std::string& id);
    bool move_item(const std::string& id, int delta);
};

Graph new_graph(const std::string& first_kind);
std::string save_graph(const Graph& g);
bool load_graph(const std::string& text, Graph& out, std::string& error);
std::string make_id();
std::string now_text();
double slider_step(const Slider& s);

} // namespace arbor

#endif // ARBOR_GRAPH_HPP
